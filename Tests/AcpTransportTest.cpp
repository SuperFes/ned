#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "Editor/Acp/Transport.h"

using ned::editor::acp::Transport;

namespace {

// A connected pipe pair wrapped as two Transports facing each other --
// writeEnd's WriteMessage is what readEnd's ReadMessage sees, and vice
// versa. No subprocess involved at all, exercising the framing logic in
// isolation. Mirrors Tests/LspTransportTest.cpp's own TransportPair exactly.
struct TransportPair {
    Transport a; // writes to b, reads from b
    Transport b; // writes to a, reads from a

    static TransportPair Create() {
        int toB[2];
        int toA[2];
        REQUIRE(::pipe(toB) == 0);
        REQUIRE(::pipe(toA) == 0);
        // a: reads toA[0], writes toB[1]. b: reads toB[0], writes toA[1].
        return TransportPair{Transport(toA[0], toB[1]), Transport(toB[0], toA[1])};
    }
};

} // namespace

TEST_CASE("Transport round-trips a simple message through a real pipe pair", "[Acp]") {
    TransportPair pair = TransportPair::Create();

    pair.a.WriteMessage(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
    const auto received = pair.b.ReadMessage();

    REQUIRE(received.has_value());
    REQUIRE(*received == R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
}

TEST_CASE("Transport round-trips a message body containing an escaped newline", "[Acp]") {
    TransportPair pair = TransportPair::Create();

    // A literal '\n' inside a JSON string is always escaped as the two
    // characters '\' 'n' by nlohmann::json::dump() -- never a raw newline
    // byte -- which is exactly what makes newline-delimited framing safe.
    // This test writes that escaped form directly (not via dump(), so it
    // stays a framing-layer test, not a JSON-serialization one).
    const std::string payload = R"({"text":"line one\nline two\nline three"})";
    pair.a.WriteMessage(payload);
    const auto received = pair.b.ReadMessage();

    REQUIRE(received.has_value());
    REQUIRE(*received == payload);
}

TEST_CASE("Transport reads multiple messages sent back to back", "[Acp]") {
    TransportPair pair = TransportPair::Create();

    pair.a.WriteMessage("first");
    pair.a.WriteMessage("second");

    const auto first  = pair.b.ReadMessage();
    const auto second = pair.b.ReadMessage();

    REQUIRE(first == "first");
    REQUIRE(second == "second");
}

TEST_CASE("Transport::ReadMessage returns nullopt on a clean EOF between messages", "[Acp]") {
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    {
        Transport writer(-1, toB[1]);
        writer.WriteMessage("only message");
        REQUIRE(reader.ReadMessage() == "only message");
        // writer goes out of scope here -- its destructor closes the write
        // end, which is what should make the next ReadMessage see a clean EOF.
    }

    REQUIRE_FALSE(reader.ReadMessage().has_value());
}

TEST_CASE("Transport::ReadMessage throws on EOF mid-message", "[Acp]") {
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    {
        Transport writer(-1, toB[1]);
        // Write bytes with no trailing newline, then let writer go out of
        // scope -- its destructor closes the write end, so EOF arrives
        // after some bytes were already read for this line, which
        // ReadMessage treats as a malformed final message rather than a
        // clean disconnect.
        const std::string partial = "{\"incomplete";
        std::size_t       written = 0;
        while (written < partial.size()) {
            const ssize_t result = ::write(toB[1], partial.data() + written, partial.size() - written);
            REQUIRE(result > 0);
            written += static_cast<std::size_t>(result);
        }
    }

    REQUIRE_THROWS_AS(reader.ReadMessage(), std::runtime_error);
}

TEST_CASE("Transport::ReadMessage does not stall on ordinary idle silence between messages", "[Acp]") {
    // subprocess-hang-protection follow-up: a very short stallTimeout must
    // never fire while genuinely waiting for a fresh message's first byte.
    TransportPair pair = TransportPair::Create();

    pair.a.WriteMessage("hello");
    const auto received = pair.b.ReadMessage(std::chrono::milliseconds(1));

    REQUIRE(received == "hello");
}

TEST_CASE("Transport::ReadMessage throws when a message stalls mid-line", "[Acp]") {
    // subprocess-hang-protection follow-up.
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    Transport writer(-1, toB[1]); // kept alive -- no EOF, just silence after the partial write

    const std::string partial = "{\"incomplete";
    std::size_t       written = 0;
    while (written < partial.size()) {
        const ssize_t result = ::write(toB[1], partial.data() + written, partial.size() - written);
        REQUIRE(result > 0);
        written += static_cast<std::size_t>(result);
    }

    REQUIRE_THROWS_AS(reader.ReadMessage(std::chrono::milliseconds(50)), std::runtime_error);
}

TEST_CASE("Transport constructor throws for an executable that can't be found on $PATH", "[Acp]") {
    REQUIRE_THROWS_AS(Transport({"ned-definitely-not-a-real-binary-xyz"}), std::runtime_error);
}

TEST_CASE("Transport constructor throws for an empty argv", "[Acp]") {
    REQUIRE_THROWS_AS(Transport(std::vector<std::string>{}), std::runtime_error);
}

TEST_CASE("Transport spawns a real process and exchanges data with it over pipes", "[Acp]") {
    // Same /bin/cat-through-stdbuf stand-in Tests/LspTransportTest.cpp uses,
    // for the same reason: cat's stdout is fully buffered against a pipe, so
    // stdbuf -o0 is what makes it flush a small payload back promptly.
    Transport transport({"stdbuf", "-o0", "/bin/cat"});

    transport.WriteMessage("hello from a test");
    const auto echoed = transport.ReadMessage();

    REQUIRE(echoed.has_value());
    REQUIRE(*echoed == "hello from a test");
}

TEST_CASE("Transport::StderrFd/ProcessLabel are unset unless constructed with captureStderr=true", "[Acp]") {
    Transport transport({"sh", "-c", "exit 0"});
    REQUIRE(transport.StderrFd() == -1);
    REQUIRE(transport.ProcessLabel() == "sh");
}

// lsp-stderr-capture follow-up (extended to ACP).
TEST_CASE("Transport captures stderr on its own pipe, separate from stdout, when captureStderr=true", "[Acp]") {
    Transport transport({"sh", "-c", "printf 'err line 1\\nerr line 2\\n' >&2"}, /*captureStderr=*/true);
    REQUIRE(transport.StderrFd() >= 0);
    REQUIRE(transport.ProcessLabel() == "sh");

    std::string collected;
    char        buffer[256];
    while (collected.find("err line 2\n") == std::string::npos) {
        const ssize_t n = ::read(transport.StderrFd(), buffer, sizeof(buffer));
        REQUIRE(n > 0);
        collected.append(buffer, static_cast<std::size_t>(n));
    }
    REQUIRE(collected == "err line 1\nerr line 2\n");

    REQUIRE_FALSE(transport.ReadMessage().has_value()); // stdout stays a clean channel -- nothing leaked across
}
