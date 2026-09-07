#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "Editor/Dap/DapManager.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Mcp/McpBridgeServer.h"
#include "Editor/Mcp/McpToolRegistry.h"
#include "Editor/Mcp/McpTransport.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::dap::DapManager;
using ned::editor::lsp::LspManager;
using ned::editor::mcp::Json;
using ned::editor::mcp::McpBridgeServer;
using ned::editor::mcp::ToolRegistry;
using ned::editor::mcp::Transport;
using ned::editor::testrun::TestRunner;
using ned::editor::vcs::VcsRunner;
using ned::text::BufferList;

namespace {

// Same "real managers, nothing configured resolves synchronously" reasoning
// Tests/McpToolRegistryTest.cpp's own Fixture documents.
struct Fixture {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         lspManager{bufferList, eventLoop};
    VcsRunner          vcsRunner{eventLoop};
    TestRunner         testRunner{bufferList, eventLoop};
    DapManager         dapManager{eventLoop};
    ToolRegistry       registry{bufferList, lspManager, vcsRunner, testRunner, dapManager};
    McpBridgeServer    server{registry, eventLoop};
};

// Real-socket integration layer, mirroring Tests/LspBrokerConnectTest.cpp's
// own shape: a background std::thread plays the MCP client (agent-spawned
// relay's role) over a real connect()ed socket, while the main test thread
// pumps eventLoop.DrainPosted_() -- the same real-timer/background-thread
// idiom Tests/LspManagerTest.cpp's own WaitUntil uses -- since
// McpBridgeServer::HandleFrame only ever runs there, never on the
// background accept/read thread.
void PumpUntil(ned::ui::EventLoop& eventLoop, const std::atomic<bool>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        eventLoop.DrainPosted_();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace

TEST_CASE("McpBridgeServer serves initialize, tools/list, and tools/call over a real socket", "[Mcp]") {
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

        transport.WriteMessage(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", "2025-06-18"}}}}.dump());
        if (const auto line = transport.ReadMessage()) {
            initLine = *line;
        }

        transport.WriteMessage(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}}.dump());
        if (const auto line = transport.ReadMessage()) {
            listLine = *line;
        }

        transport.WriteMessage(
            Json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"}, {"params", {{"name", "get_test_results"}, {"arguments", Json::object()}}}}
                .dump());
        if (const auto line = transport.ReadMessage()) {
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
    REQUIRE(tools.size() == 37);
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

TEST_CASE("McpBridgeServer answers an unknown tool name with a JSON-RPC error", "[Mcp]") {
    Fixture fixture;
    fixture.server.Start();

    std::atomic<bool> done{false};
    std::string       errorLine;

    std::thread client([&] {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
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
        transport.WriteMessage(
            Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", {{"name", "not_a_real_tool"}, {"arguments", Json::object()}}}}
                .dump());
        if (const auto line = transport.ReadMessage()) {
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
