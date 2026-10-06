#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "BoundedRead.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Protocol/FrameTransport.h"
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
        const ssize_t n = ned::test::BoundedRead(fd, buffer, sizeof(buffer));
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

TEST_CASE("A stderr line posted before the connection is destroyed is still logged", "[Protocol]") {
    ned::ui::EventLoop eventLoop;
    auto               connection = std::make_unique<FramedConnection<Transport>>(
        Transport({"sh", "-c", "echo framed-stderr-after-close >&2"}, /*captureStderr=*/true), eventLoop,
        FramedConnection<Transport>::Options{LogCategory::Dap, "adapter", ned::editor::LogSeverity::Error});

    // The line is posted from the stderr thread; it must reach the loop's
    // queue before the connection goes.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    connection.reset();
    (void)eventLoop.DrainPosted_();

    const std::vector<ned::editor::LogEntry> entries = ned::editor::LogEntries();
    const auto                               entry   = std::ranges::find_if(entries, [](const ned::editor::LogEntry& e) {
        return e.message.find("framed-stderr-after-close") != std::string::npos;
    });
    REQUIRE(entry != entries.end());
    REQUIRE(entry->category == LogCategory::Dap);
    REQUIRE(entry->severity == ned::editor::LogSeverity::Error);
}

// A peer that keeps its end open says nothing about this side wanting to
// stop; teardown must not depend on the peer hanging up first.
TEST_CASE("FramedConnection is destroyed promptly while its peer keeps the pipes open", "[FramedConnection][Lifetime]") {
    ned::ui::EventLoop eventLoop;
    int                oursWritesHere[2];
    int                oursReadsHere[2];
    REQUIRE(::pipe(oursWritesHere) == 0);
    REQUIRE(::pipe(oursReadsHere) == 0);

    std::optional<FramedConnection<Transport>> connection(std::in_place, Transport(oursReadsHere[0], oursWritesHere[1]), eventLoop,
                                                          ConnectionFixture::MakeOptions());
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the read loop is parked on the pipe

    std::atomic<bool> destroyed{false};
    std::thread       destroyer([&] {
        connection.reset();
        destroyed = true;
    });
    const auto        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!destroyed && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool prompt = destroyed.load();
    ::close(oursReadsHere[1]); // the peer hanging up is what unblocks a hung destroyer
    destroyer.join();
    ::close(oursWritesHere[0]);

    REQUIRE(prompt);
}

namespace {

using FrameConnection = FramedConnection<ned::editor::protocol::FrameTransport>;

// A peer that ignores flow control, writing frames as fast as the pipe takes
// them, against a connection whose read-ahead is capped at maxQueuedBytes.
struct Flood {
    ned::ui::EventLoop             eventLoop;
    int                            toEditor[2]{-1, -1};
    int                            fromEditor[2]{-1, -1};
    std::optional<FrameConnection> connection;
    std::vector<std::string>       received;
    std::string                    stream; // every frame the peer means to send
    std::size_t                    sent = 0;

    Flood(std::size_t maxQueuedBytes, std::size_t frames, std::size_t payloadBytes) {
        REQUIRE(::pipe(toEditor) == 0);
        REQUIRE(::pipe(fromEditor) == 0);
        REQUIRE(::fcntl(toEditor[1], F_SETFL, O_NONBLOCK) == 0);
        connection.emplace(ned::editor::protocol::FrameTransport(toEditor[0], fromEditor[1]), eventLoop,
                           FrameConnection::Options{.logCategory = LogCategory::General, .entityNoun = "agent", .maxQueuedBytes = maxQueuedBytes});
        connection->SetOnFrame([this](std::string frame) { received.push_back(std::move(frame)); });
        for (std::size_t i = 0; i < frames; ++i) {
            std::string payload = std::to_string(i) + ":";
            payload.resize(payloadBytes, 'x');
            stream += ned::editor::protocol::EncodeFrame(payload, false);
        }
    }
    ~Flood() {
        connection.reset();
        ::close(toEditor[1]);
        ::close(fromEditor[0]);
    }
    Flood(const Flood&)            = delete;
    Flood& operator=(const Flood&) = delete;

    // Writes until the pipe has refused more for quiet, without draining.
    void FillUntilRefused(std::chrono::milliseconds quiet = std::chrono::milliseconds(300)) {
        auto lastProgress = std::chrono::steady_clock::now();
        while (sent < stream.size() && std::chrono::steady_clock::now() - lastProgress < quiet) {
            if (WriteSome()) {
                lastProgress = std::chrono::steady_clock::now();
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
    bool WriteSome() {
        const ssize_t n = ::write(toEditor[1], stream.data() + sent, stream.size() - sent);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
        return true;
    }
};

} // namespace

TEST_CASE("A peer ignoring flow control fills its own pipe, not the editor's queue", "[Protocol][FramedConnection]") {
    constexpr std::size_t kCap     = 64 * 1024;
    constexpr std::size_t kPayload = 4096;
    constexpr std::size_t kFrames  = 1024;
    Flood                 flood(kCap, kFrames, kPayload);

    flood.FillUntilRefused();
    // The reader stops past the cap: what got in is the cap, the frame in
    // hand, and what the pipe itself holds.
    const auto pipeBytes = static_cast<std::size_t>(::fcntl(flood.toEditor[1], F_GETPIPE_SZ));
    REQUIRE(flood.sent <= kCap + 2 * (kPayload + 512) + pipeBytes);
    REQUIRE(flood.sent < flood.stream.size());

    // Draining lets the rest through, in order.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (flood.received.size() < kFrames && std::chrono::steady_clock::now() < deadline) {
        flood.WriteSome();
        flood.eventLoop.DrainPosted_();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(flood.received.size() == kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) {
        REQUIRE(flood.received[i].starts_with(std::to_string(i) + ":"));
    }
}

TEST_CASE("A frame larger than the read-ahead cap still arrives", "[Protocol][FramedConnection]") {
    Flood      flood(1024, 2, 256 * 1024);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (flood.received.size() < 2 && std::chrono::steady_clock::now() < deadline) {
        flood.WriteSome();
        flood.eventLoop.DrainPosted_();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(flood.received.size() == 2);
    REQUIRE(flood.received[1].size() == 256 * 1024);
}

TEST_CASE("A connection whose reader is held back by the cap is destroyed promptly", "[Protocol][FramedConnection][Lifetime]") {
    Flood flood(16 * 1024, 1024, 4096);
    flood.FillUntilRefused();
    REQUIRE(flood.sent < flood.stream.size());

    const auto started = std::chrono::steady_clock::now();
    flood.connection.reset();
    REQUIRE(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
}
