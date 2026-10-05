#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "Editor/Dap/Manager.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Mcp/BridgeServer.h"
#include "Editor/Mcp/ToolRegistry.h"
#include "Editor/Protocol/LineTransport.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/Runner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::mcp::BridgeServer;
using ned::editor::mcp::Json;
using ned::editor::mcp::ToolRegistry;
using Transport = ned::editor::protocol::LineTransport;
using ned::editor::testrun::TestRunner;
using ned::editor::vcs::Runner;
using ned::text::BufferList;

namespace {

// Same "real managers, nothing configured resolves synchronously" reasoning
// Tests/McpToolRegistryTest.cpp's own Fixture documents.
struct Fixture {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    ned::editor::lsp::Manager lspManager{bufferList, eventLoop};
    Runner          vcsRunner{eventLoop};
    TestRunner         testRunner{bufferList, eventLoop};
    ned::editor::dap::Manager dapManager{eventLoop};
    ToolRegistry       registry{bufferList, lspManager, vcsRunner, testRunner, dapManager};
    BridgeServer    server{registry, eventLoop};
};

// Real-socket integration layer, mirroring Tests/LspBrokerConnectTest.cpp's
// own shape: a background std::thread plays the MCP client (agent-spawned
// relay's role) over a real connect()ed socket, while the main test thread
// pumps eventLoop.DrainPosted_() -- the same real-timer/background-thread
// idiom Tests/ManagerTest.cpp's own WaitUntil uses -- since
// BridgeServer::HandleFrame only ever runs there, never on the
// background accept/read thread.
void PumpUntil(ned::ui::EventLoop& eventLoop, const std::atomic<bool>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        eventLoop.DrainPosted_();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// Connects a relay-side transport to path, or nothing if connect() fails.
std::optional<Transport> ConnectRelay(const std::filesystem::path& path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return std::nullopt;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return std::nullopt;
    }
    return Transport(fd, ::dup(fd), -1);
}

Json Request(int id, const std::string& method) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
}

} // namespace

TEST_CASE("BridgeServer serves initialize, tools/list, and tools/call over a real socket", "[Mcp]") {
    Fixture fixture;
    fixture.server.Start();
    REQUIRE(fixture.server.IsListening());

    std::atomic<bool> done{false};
    std::string       initLine;
    std::string       listLine;
    std::string       callLine;

    std::thread client([&] {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            done = true;
            return;
        }
        sockaddr_un addr{};
        addr.sun_family        = AF_UNIX;
        const std::string path = fixture.server.SocketPath().string();
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            done = true;
            return;
        }
        const int dupFd = ::dup(fd);
        Transport transport(fd, dupFd, -1);

        transport.WriteFrame(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", "2025-06-18"}}}}.dump());
        if (const auto line = transport.ReadFrame()) {
            initLine = *line;
        }

        transport.WriteFrame(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}}.dump());
        if (const auto line = transport.ReadFrame()) {
            listLine = *line;
        }

        transport.WriteFrame(
            Json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"}, {"params", {{"name", "get_test_results"}, {"arguments", Json::object()}}}}
                .dump());
        if (const auto line = transport.ReadFrame()) {
            callLine = *line;
        }
        done = true;
    });

    PumpUntil(fixture.eventLoop, done);
    client.join();

    REQUIRE_FALSE(initLine.empty());
    const Json initResponse = Json::parse(initLine);
    REQUIRE(initResponse.at("id") == 1);
    REQUIRE(initResponse.at("result").at("serverInfo").at("name") == "ned");
    REQUIRE(initResponse.at("result").at("protocolVersion") == "2025-06-18");

    REQUIRE_FALSE(listLine.empty());
    const Json listResponse = Json::parse(listLine);
    REQUIRE(listResponse.at("id") == 2);
    const Json& tools = listResponse.at("result").at("tools");
    REQUIRE(tools.size() == fixture.registry.ListTools().size());
    bool foundGetTestResults = false;
    for (const Json& tool : tools) {
        if (tool.at("name") == "get_test_results") {
            foundGetTestResults = true;
        }
        REQUIRE(tool.contains("description"));
        REQUIRE(tool.contains("inputSchema"));
    }
    REQUIRE(foundGetTestResults);

    REQUIRE_FALSE(callLine.empty());
    const Json callResponse = Json::parse(callLine);
    REQUIRE(callResponse.at("id") == 3);
    const std::string text = callResponse.at("result").at("content").at(0).at("text").get<std::string>();
    REQUIRE(text.find("No test results yet") != std::string::npos);
    REQUIRE_FALSE(callResponse.at("result").value("isError", false));
}

TEST_CASE("BridgeServer answers an unknown tool name with a JSON-RPC error", "[Mcp]") {
    Fixture fixture;
    fixture.server.Start();

    std::atomic<bool> done{false};
    std::string       errorLine;

    std::thread client([&] {
        const int   fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family        = AF_UNIX;
        const std::string path = fixture.server.SocketPath().string();
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            done = true;
            return;
        }
        const int dupFd = ::dup(fd);
        Transport transport(fd, dupFd, -1);
        transport.WriteFrame(
            Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", {{"name", "not_a_real_tool"}, {"arguments", Json::object()}}}}
                .dump());
        if (const auto line = transport.ReadFrame()) {
            errorLine = *line;
        }
        done = true;
    });

    PumpUntil(fixture.eventLoop, done);
    client.join();

    REQUIRE_FALSE(errorLine.empty());
    const Json response = Json::parse(errorLine);
    REQUIRE(response.contains("error"));
    REQUIRE(response.at("error").at("code") == -32601);
}

TEST_CASE("BridgeServer serves a second relay while the first is still connected", "[Mcp]") {
    Fixture fixture;
    fixture.server.Start();

    std::atomic<bool> done{false};
    std::string       firstLine;
    std::string       secondLine;

    std::thread client([&] {
        std::optional<Transport> first = ConnectRelay(fixture.server.SocketPath());
        if (first) {
            first->WriteFrame(Request(1, "initialize").dump());
            firstLine = first->ReadFrame().value_or("");
        }
        // first stays open across the second relay's whole exchange.
        if (std::optional<Transport> second = ConnectRelay(fixture.server.SocketPath())) {
            second->WriteFrame(Request(2, "tools/list").dump());
            secondLine = second->ReadFrame(std::chrono::seconds(2)).value_or("");
        }
        done = true;
    });

    PumpUntil(fixture.eventLoop, done);
    client.join();

    REQUIRE_FALSE(firstLine.empty());
    CHECK(Json::parse(firstLine).at("id") == 1);
    REQUIRE_FALSE(secondLine.empty());
    CHECK(Json::parse(secondLine).at("id") == 2);
}

TEST_CASE("Destroying a BridgeServer with a relay connected returns promptly and hangs up the relay", "[Mcp][Lifetime]") {
    Fixture                     fixture;
    std::optional<BridgeServer> server(std::in_place, fixture.registry, fixture.eventLoop);
    server->Start();

    std::optional<Transport> relay = ConnectRelay(server->SocketPath());
    REQUIRE(relay);

    // A round trip proves the connection was adopted before teardown.
    std::atomic<bool> done{false};
    std::string       reply;
    std::thread       client([&] {
        relay->WriteFrame(Request(1, "initialize").dump());
        reply = relay->ReadFrame().value_or("");
        done  = true;
    });
    PumpUntil(fixture.eventLoop, done);
    client.join();
    REQUIRE_FALSE(reply.empty());

    const auto start = std::chrono::steady_clock::now();
    server.reset();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    CHECK_FALSE(relay->ReadFrame().has_value());
}
