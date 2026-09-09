#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "Editor/Mcp/McpTransport.h"

using ned::editor::mcp::Transport;

namespace {

// Mirrors Tests/AcpTransportTest.cpp's/Tests/LspTransportTest.cpp's own
// TransportPair exactly -- a connected pipe pair wrapped as two Transports
// facing each other, no subprocess involved.
struct TransportPair {
    Transport a; // writes to b, reads from b
    Transport b; // writes to a, reads from a

    static TransportPair Create() {
        int toB[2];
        int toA[2];
        REQUIRE(::pipe(toB) == 0);
        REQUIRE(::pipe(toA) == 0);
        return TransportPair{Transport(toA[0], toB[1]), Transport(toB[0], toA[1])};
    }
};

} // namespace

TEST_CASE("Mcp::Transport round-trips a simple message through a real pipe pair", "[Mcp]") {
    TransportPair pair = TransportPair::Create();

    pair.a.WriteMessage(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    const auto received = pair.b.ReadMessage();

    REQUIRE(received.has_value());
    REQUIRE(*received == R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
}

TEST_CASE("Mcp::Transport reads multiple messages sent back to back", "[Mcp]") {
    TransportPair pair = TransportPair::Create();

    pair.a.WriteMessage("first");
    pair.a.WriteMessage("second");

    const auto first  = pair.b.ReadMessage();
    const auto second = pair.b.ReadMessage();

    REQUIRE(first == "first");
    REQUIRE(second == "second");
}

TEST_CASE("Mcp::Transport::ReadMessage returns nullopt on a clean EOF between messages", "[Mcp]") {
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    {
        Transport writer(-1, toB[1]);
        writer.WriteMessage("only message");
        REQUIRE(reader.ReadMessage() == "only message");
    }

    REQUIRE_FALSE(reader.ReadMessage().has_value());
}

TEST_CASE("Mcp::Transport::ReadMessage throws on EOF mid-message", "[Mcp]") {
    int toB[2];
    REQUIRE(::pipe(toB) == 0);
    Transport reader(toB[0], -1);
    {
        Transport         writer(-1, toB[1]);
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

TEST_CASE("Mcp::Transport::ReadMessage does not stall on ordinary idle silence between messages", "[Mcp]") {
    TransportPair pair = TransportPair::Create();

    pair.a.WriteMessage("hello");
    const auto received = pair.b.ReadMessage(std::chrono::milliseconds(1));

    REQUIRE(received == "hello");
}

TEST_CASE("Mcp::Transport constructor throws for an empty argv", "[Mcp]") {
    REQUIRE_THROWS_AS(Transport(std::vector<std::string>{}), std::runtime_error);
}

TEST_CASE("Mcp::Transport spawns a real process and exchanges data with it over pipes", "[Mcp]") {
    Transport transport({"stdbuf", "-o0", "/bin/cat"});

    transport.WriteMessage("hello from a test");
    const auto echoed = transport.ReadMessage();

    REQUIRE(echoed.has_value());
    REQUIRE(*echoed == "hello from a test");
}
