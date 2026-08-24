#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "Editor/Lsp/Transport.h"

using ned::editor::lsp::Transport;

namespace {

// A connected pipe pair wrapped as two Transports facing each other --
// writeEnd's WriteFrame is what readEnd's ReadFrame sees, and vice versa.
// No subprocess involved at all, exercising the framing logic in isolation.
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

TEST_CASE("Transport round-trips a simple frame through a real pipe pair", "[Lsp]") {
    TransportPair pair = TransportPair::Create();

    pair.a.WriteFrame(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
    const auto received = pair.b.ReadFrame();

    REQUIRE(received.has_value());
    REQUIRE(*received == R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
}

TEST_CASE("Transport round-trips a frame body containing embedded newlines", "[Lsp]") {
    TransportPair pair = TransportPair::Create();

    const std::string payload = "{\"text\":\"line one\\nline two\\nline three\"}";
    pair.a.WriteFrame(payload);
    const auto received = pair.b.ReadFrame();

    REQUIRE(received.has_value());
    REQUIRE(*received == payload);
}

TEST_CASE("Transport reads multiple frames sent back to back", "[Lsp]") {
    TransportPair pair = TransportPair::Create();

    pair.a.WriteFrame("first");
    pair.a.WriteFrame("second");

    const auto first  = pair.b.ReadFrame();
    const auto second = pair.b.ReadFrame();

    REQUIRE(first == "first");
    REQUIRE(second == "second");
}

TEST_CASE("Transport::ReadFrame returns nullopt on a clean EOF between frames", "[Lsp]") {
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    {
        Transport writer(-1, toB[1]);
        writer.WriteFrame("only frame");
        REQUIRE(reader.ReadFrame() == "only frame");
        // writer goes out of scope here -- its destructor closes the write
        // end, which is what should make the next ReadFrame see a clean EOF.
    }

    REQUIRE_FALSE(reader.ReadFrame().has_value());
}

TEST_CASE("Transport::ReadFrame throws on a frame with no Content-Length header", "[Lsp]") {
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    Transport writer(-1, toB[1]);

    // Hand-written malformed frame: a header block with no Content-Length at
    // all before the blank line, matching what WriteFrame itself would never
    // produce -- this is testing ReadFrame's own validation, not round-trip
    // behavior.
    const std::string malformed = "X-Something: 1\r\n\r\n";
    std::size_t       written   = 0;
    while (written < malformed.size()) {
        const ssize_t result = ::write(toB[1], malformed.data() + written, malformed.size() - written);
        REQUIRE(result > 0);
        written += static_cast<std::size_t>(result);
    }

    REQUIRE_THROWS_AS(reader.ReadFrame(), std::runtime_error);
}

TEST_CASE("Transport::ReadFrame does not stall on ordinary idle silence between frames", "[Lsp]") {
    // subprocess-hang-protection follow-up: a very short stallTimeout must
    // never fire while genuinely waiting for the *first* byte of a fresh
    // frame -- only mid-frame silence is bounded.
    TransportPair pair = TransportPair::Create();

    pair.a.WriteFrame("hello");
    const auto received = pair.b.ReadFrame(std::chrono::milliseconds(1));

    REQUIRE(received == "hello");
}

TEST_CASE("Transport::ReadFrame throws when a frame stalls mid-header", "[Lsp]") {
    // subprocess-hang-protection follow-up.
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    Transport writer(-1, toB[1]);

    // Partial header line, no terminating "\r\n\r\n", and nothing more ever
    // arrives -- exactly what a stuck server looks like mid-message.
    const std::string partial = "Content-Le";
    std::size_t       written = 0;
    while (written < partial.size()) {
        const ssize_t result = ::write(toB[1], partial.data() + written, partial.size() - written);
        REQUIRE(result > 0);
        written += static_cast<std::size_t>(result);
    }

    REQUIRE_THROWS_AS(reader.ReadFrame(std::chrono::milliseconds(50)), std::runtime_error);
}

TEST_CASE("Transport::ReadFrame throws when a frame stalls mid-body", "[Lsp]") {
    // subprocess-hang-protection follow-up.
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    Transport writer(-1, toB[1]);

    // A complete, valid header announcing 5 body bytes, but only 2 ever
    // arrive.
    const std::string partial = "Content-Length: 5\r\n\r\nab";
    std::size_t       written = 0;
    while (written < partial.size()) {
        const ssize_t result = ::write(toB[1], partial.data() + written, partial.size() - written);
        REQUIRE(result > 0);
        written += static_cast<std::size_t>(result);
    }

    REQUIRE_THROWS_AS(reader.ReadFrame(std::chrono::milliseconds(50)), std::runtime_error);
}

TEST_CASE("Transport constructor throws for an executable that can't be found on $PATH", "[Lsp]") {
    REQUIRE_THROWS_AS(Transport({"ned-definitely-not-a-real-binary-xyz"}), std::runtime_error);
}

TEST_CASE("Transport constructor throws for an empty argv", "[Lsp]") {
    REQUIRE_THROWS_AS(Transport(std::vector<std::string>{}), std::runtime_error);
}

TEST_CASE("Transport spawns a real process and exchanges data with it over pipes", "[Lsp]") {
    // /bin/cat is present on every POSIX system this project targets and
    // makes a trivial stand-in "server": whatever's written to its stdin
    // comes back out its stdout, verbatim, with no framing of its own -- so
    // this proves SpawnLanguageServer's pipe-wiring/posix_spawn path
    // actually works end to end, distinct from the framing-only tests above
    // which never spawn a real process at all. Run through `stdbuf -o0`
    // (coreutils, present alongside cat on every system this targets) --
    // real, hung-test-confirmed reason, not a defensive guess: cat's stdout
    // is fully buffered (not line-buffered) whenever it isn't a real tty,
    // which a pipe never is, so it would otherwise sit on this test's small
    // payload indefinitely instead of ever flushing it back.
    Transport transport({"stdbuf", "-o0", "/bin/cat"});

    transport.WriteFrame("hello from a test");
    const auto echoed = transport.ReadFrame();

    REQUIRE(echoed.has_value());
    REQUIRE(*echoed == "hello from a test");
}
