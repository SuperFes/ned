//
// ACP MCP tool-server bridge, slice 1. Owns the live `ned` process's own
// listening Unix domain socket (McpSocketPath.h) and speaks just enough of
// the real MCP protocol (initialize / notifications/initialized / tools/list
// / tools/call, newline-delimited JSON-RPC via McpTransport) to serve one
// connected client at a time -- the `ned --mcp-stdio-relay` subprocess the
// ACP agent spawns as its configured stdio MCP server (see main.cpp and
// AcpManager::StartSession's mcpServers payload). Real tool dispatch is
// delegated entirely to one injected ToolRegistry; this class is pure
// protocol/transport plumbing.
//
// Threading shape mirrors every other background-I/O class in this codebase
// (LspClient/DapClient/AcpClient's own read loops): a background jthread
// does blocking accept()/ReadMessage() calls only, marshaling each complete
// line onto the main thread via EventLoop::Post before touching any real
// state -- ToolRegistry::CallTool, and therefore every LspManager/VcsRunner/
// TestRunner call it makes, always runs on the main thread. A tool's
// eventual response write (SendResult/SendError) also happens on the main
// thread, synchronously via McpTransport::WriteMessage -- MCP tool results
// here are small single-line JSON blobs, so this deliberately skips the
// async-write-queue treatment LspClient needed for large payloads (that was
// a fix for a *measured* freeze on big workspace-wide operations, not a
// speculative one; revisit only if the same is ever measured here).
//
// Only one connection is served at a time (AcpManager itself is a single
// session, so at most one relay is ever expected live) -- the accept loop
// simply waits for the next connection once the current one disconnects.
//

#ifndef NED_EDITOR_MCP_MCPBRIDGESERVER_H
#define NED_EDITOR_MCP_MCPBRIDGESERVER_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <thread>

#include <nlohmann/json.hpp>

#include "UI/EventLoop.h"

#include "McpTransport.h"

namespace ned::editor::mcp {

class ToolRegistry;

using Json = nlohmann::json;

class McpBridgeServer {
  public:
    // registry and eventLoop must both outlive this McpBridgeServer, same
    // requirement every sibling manager in this codebase documents.
    McpBridgeServer(ToolRegistry& registry, ned::ui::EventLoop& eventLoop);

    // Closes the listening socket and any live connection (unblocking the
    // background accept/read loop via shutdown(2), not a second close of an
    // fd Transport's own destructor still owns) before the jthread member's
    // own destructor joins it -- the "close the fd the background thread is
    // blocked on" pattern LspClient's own transport_/readThread_ member
    // ordering already establishes.
    ~McpBridgeServer();

    McpBridgeServer(const McpBridgeServer&)            = delete;
    McpBridgeServer& operator=(const McpBridgeServer&) = delete;

    // Binds and starts listening on McpSocketPathForPid(getpid()) -- a no-op
    // if already listening. Throws std::runtime_error on failure (runtime
    // directory creation, socket/bind/listen syscall failure).
    void Start();

    [[nodiscard]] bool                         IsListening() const noexcept;
    [[nodiscard]] const std::filesystem::path& SocketPath() const noexcept;

  private:
    void AcceptLoop(std::stop_token stopToken);
    // Runs on the background thread for one accepted connection until
    // ReadMessage() returns std::nullopt (EOF/disconnect) or throws (a
    // genuine transport error, logged and treated the same as a disconnect).
    void ServeConnection(const std::shared_ptr<Transport>& transport, std::stop_token stopToken);

    // Posted onto the main thread per complete line. Parses the JSON-RPC
    // frame and dispatches by method.
    void HandleFrame(const std::shared_ptr<Transport>& transport, const std::string& line);

    void SendResult(const std::weak_ptr<Transport>& transport, const Json& id, const Json& result);
    void SendError(const std::weak_ptr<Transport>& transport, const Json& id, int code, const std::string& message);

    ToolRegistry&         registry_;
    ned::ui::EventLoop&   eventLoop_;
    std::filesystem::path socketPath_;
    int                   listenFd_ = -1;
    std::atomic<int>      currentConnFd_{-1};
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::jthread          acceptThread_; // declared last: its destructor (auto stop+join) must run only after ~McpBridgeServer's body has already unblocked it
};

} // namespace ned::editor::mcp

#endif // NED_EDITOR_MCP_MCPBRIDGESERVER_H
