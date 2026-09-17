#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <unistd.h>

#include "Editor/Lsp/Transport.h"
#include "Editor/Protocol/FramedConnection.h"
#include "UI/EventLoop.h"

using ned::editor::LogCategory;
using ned::editor::lsp::Transport;
using ned::editor::protocol::FramedConnection;

namespace {

// Mirrors Tests/LspClientTest.cpp's own ClientFixture exactly (see that
// file's header comment for the full reasoning behind every piece of this
// shape) -- one end of a pipe pair wrapped as a Transport for the
// FramedConnection under test, the other end left as raw fds the test reads/
// writes directly, standing in for "the remote process's own stdin/stdout."
struct ConnectionFixture {
    ned::ui::EventLoop            eventLoop;
    int                           peerReadsFromUs;  // test reads what the connection wrote
    int                           peerWritesToUs;   // test writes to feed the connection's read thread
    FramedConnection<Transport> connection;

    ConnectionFixture(int readFd, int writeFd, Transport transport)
        : peerReadsFromUs(readFd), peerWritesToUs(writeFd), connection(std::move(transport), eventLoop, MakeOptions()) {
    }

    ~ConnectionFixture() {
        ::close(peerWritesToUs);
        ::close(peerReadsFromUs);
    }

    ConnectionFixture(const ConnectionFixture&)            = delete;
    ConnectionFixture& operator=(const ConnectionFixture&) = delete;

    static ConnectionFixture Create() {
        int oursWritesHere[2]; // connection's write end -> test's read end
        int oursReadsHere[2];  // test's write end -> connection's read end
        REQUIRE(::pipe(oursWritesHere) == 0);
        REQUIRE(::pipe(oursReadsHere) == 0);
        return ConnectionFixture(oursWritesHere[0], oursReadsHere[1], Transport(oursReadsHere[0], oursWritesHere[1]));
    }

    static FramedConnection<Transport>::Options MakeOptions() {
        return {LogCategory::Lsp, "server", ned::editor::LogSeverity::Warning};
    }
};

// Reads exactly one Content-Length frame's body off a plain fd -- mirrors
// Transport::ReadFrame's own framing logic independently (already separately
// tested in LspTransportTest.cpp), so these tests verify SendFrame's actual
// wire output without depending on that parser.
std::string ReadRawFrameBody(int fd) {
    std::string all;
    char        buffer[256];
    for (int i = 0; i < 4; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
        const auto headerEnd = all.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            constexpr std::string_view kPrefix   = "Content-Length: ";
            const auto                 prefixPos = all.find(kPrefix);
            if (prefixPos != std::string::npos) {
                const std::size_t contentLength = std::stoul(all.substr(prefixPos + kPrefix.size()));
                if (all.size() >= headerEnd + 4 + contentLength) {
                    return all.substr(headerEnd + 4, contentLength);
                }
            }
        }
    }
    return all;
}

void WriteFramedMessage(int fd, std::string_view payload) {
    const std::string frame = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + std::string(payload);
    REQUIRE(::write(fd, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()));
}

} // namespace

TEST_CASE("FramedConnection::SendFrame writes the frame through the transport", "[Protocol]") {
    ConnectionFixture fixture = ConnectionFixture::Create();

    fixture.connection.SendFrame("hello");

    REQUIRE(ReadRawFrameBody(fixture.peerReadsFromUs) == "hello");
}

TEST_CASE("FramedConnection delivers an arriving frame via SetOnFrame, marshaled through EventLoop::Post", "[Protocol]") {
    ConnectionFixture fixture = ConnectionFixture::Create();

    std::vector<std::string> received;
    fixture.connection.SetOnFrame([&](std::string frame) { received.push_back(std::move(frame)); });

    WriteFramedMessage(fixture.peerWritesToUs, "payload-one");

    // The background read loop parses the frame and Post()s it; nothing runs
    // it until we drain -- the same real background-thread -> Post() -> drain
    // cycle Tests/LspClientTest.cpp's UAF regression test uses.
    for (int i = 0; i < 200 && received.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        fixture.eventLoop.DrainPosted_();
    }

    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == "payload-one");
}

// Managed without ConnectionFixture (whose destructor closes both ends of
// the pipe unconditionally) since this test needs to close the peer's write
// end itself, mid-test, to trigger EOF -- mirrors
// Tests/LspClientTest.cpp's own "A stray Post()ed callback..." test, which
// takes the same raw-fd approach for the identical reason.
TEST_CASE("FramedConnection reports EOF via SetOnDisconnected with the configured entity noun", "[Protocol]") {
    ned::ui::EventLoop eventLoop;
    int                oursWritesHere[2];
    int                oursReadsHere[2];
    REQUIRE(::pipe(oursWritesHere) == 0);
    REQUIRE(::pipe(oursReadsHere) == 0);
    const int peerReadsFromUs = oursWritesHere[0];
    const int peerWritesToUs  = oursReadsHere[1];

    FramedConnection<Transport> connection(Transport(oursReadsHere[0], oursWritesHere[1]), eventLoop, ConnectionFixture::MakeOptions());

    std::optional<std::string> reason;
    connection.SetOnDisconnected([&](std::string r) { reason = std::move(r); });

    ::close(peerWritesToUs); // EOF from the connection's own read side

    for (int i = 0; i < 200 && !reason.has_value(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        eventLoop.DrainPosted_();
    }

    REQUIRE(reason.has_value());
    REQUIRE(*reason == "server exited (EOF)");

    ::close(peerReadsFromUs);
}

TEST_CASE("FramedConnection::PrepareForGracefulShutdown drains a queued frame before the writer thread stops", "[Protocol]") {
    ConnectionFixture fixture = ConnectionFixture::Create();

    fixture.connection.PrepareForGracefulShutdown();
    fixture.connection.SendFrame("shutdown-marker");

    REQUIRE(ReadRawFrameBody(fixture.peerReadsFromUs) == "shutdown-marker");
}

TEST_CASE("Ordinary FramedConnection destruction (no PrepareForGracefulShutdown) does not hang", "[Protocol]") {
    // Regression coverage for the writeCv_ condition_variable_any fix this
    // class inherited from Client.h -- see FramedConnection.h's own header
    // comment. Destroying a connection with an idle (empty-queue) writer
    // thread must return promptly.
    const auto start = std::chrono::steady_clock::now();
    {
        ConnectionFixture fixture = ConnectionFixture::Create();
        fixture.connection.SendFrame("marker");
        (void)ReadRawFrameBody(fixture.peerReadsFromUs); // let the queue drain to empty before destruction
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::seconds(2));
}

// closed-connection-never-parks follow-up, inherited from Client.h -- see
// Tests/LspClientTest.cpp's identical "Destroying a Client before its read
// thread has started doesn't deadlock" for the full reasoning. Hammering
// construct-then-immediately-destroy is what makes the scheduler land in the
// window where a not-yet-scheduled read thread's first read would otherwise
// park forever in poll() on an already-torn-down transport.
TEST_CASE("Destroying a FramedConnection before its read thread has started doesn't deadlock", "[Protocol]") {
    ned::ui::EventLoop eventLoop;

    for (int iteration = 0; iteration < 200; ++iteration) {
        int oursWritesHere[2];
        int oursReadsHere[2];
        REQUIRE(::pipe(oursWritesHere) == 0);
        REQUIRE(::pipe(oursReadsHere) == 0);

        {
            FramedConnection<Transport> connection(Transport(oursReadsHere[0], oursWritesHere[1]), eventLoop, ConnectionFixture::MakeOptions());
            ::close(oursReadsHere[1]);
        }

        ::close(oursWritesHere[0]);
    }
    SUCCEED();
}
