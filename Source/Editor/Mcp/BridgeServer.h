//
// ACP MCP tool-server bridge, slice 1. Owns the live `ned` process's own
// listening Unix domain socket (SocketPath.h) and speaks just enough of
// the real MCP protocol (initialize / notifications/initialized / tools/list
// / tools/call, newline-delimited JSON-RPC) to serve the relays that connect
// to it -- the `ned --mcp-stdio-relay` subprocess the
// ACP agent spawns as its configured stdio MCP server (see main.cpp and
// Manager::StartSession's mcpServers payload). Real tool dispatch is
// delegated entirely to one injected ToolRegistry; this class is pure
// protocol/transport plumbing.
//
// A background jthread only accept()s, handing each connected socket to the
// main thread, where it becomes one FramedConnection: that class's reader
// and async writer threads, and its teardown order, are what serve the
// relay. ToolRegistry::CallTool, and therefore every Manager/Runner/
// TestRunner call it makes, always runs on the main thread. Each live relay
// gets its own connection, so a second relay is served alongside the first
// rather than queued behind it.
//

#ifndef NED_EDITOR_MCP_BRIDGESERVER_H
#define NED_EDITOR_MCP_BRIDGESERVER_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "Editor/Lifetime.h"
#include "Editor/Protocol/FramedConnection.h"
#include "Editor/Protocol/LineTransport.h"
#include "UI/EventLoop.h"

namespace ned::editor::mcp {

class ToolRegistry;

using Json = nlohmann::json;

class BridgeServer {
  public:
    // registry and eventLoop must both outlive this BridgeServer, same
    // requirement every sibling manager in this codebase documents.
    BridgeServer(ToolRegistry& registry, ned::ui::EventLoop& eventLoop);

    // Hangs up the listening socket, joins the accept loop, then closes the
    // socket and every live connection.
    ~BridgeServer();

    BridgeServer(const BridgeServer&)            = delete;
    BridgeServer& operator=(const BridgeServer&) = delete;

    // Binds and starts listening on SocketPathForPid(getpid()) -- a no-op
    // if already listening. Throws std::runtime_error on failure (runtime
    // directory creation, socket/bind/listen syscall failure).
    void Start();

    [[nodiscard]] bool                         IsListening() const noexcept;
    [[nodiscard]] const std::filesystem::path& SocketPath() const noexcept;

    // A relay on a host (Host/McpRelays.h), whose lines arrive through
    // ReceiveRemote and whose answers go to send. Its tool calls name that
    // host's files: an absolute "file" argument is read as host:/path.
    // Returns its id for the other two. Main thread only.
    std::uint64_t AdoptRemote(std::string host, std::function<void(std::string line)> send);
    void          ReceiveRemote(std::uint64_t id, const std::string& line);
    void          CloseRemote(std::uint64_t id);

  private:
    using Connection   = protocol::FramedConnection<protocol::LineTransport>;
    using ConnectionId = std::uint64_t;

    void AcceptLoop(const std::stop_token& stopToken, const LifetimeToken& alive);
    void Adopt(protocol::LineTransport transport);

    // Parses one JSON-RPC frame from connection id and dispatches by method.
    void HandleFrame(ConnectionId id, const std::string& line);

    void SendResult(ConnectionId id, const Json& requestId, const Json& result);
    void SendError(ConnectionId id, const Json& requestId, int code, const std::string& message);
    // Dropped when the connection has gone, or for a notification (no id).
    void Send(ConnectionId id, const Json& requestId, Json response);

    ToolRegistry&         registry_;
    ned::ui::EventLoop&   eventLoop_;
    std::filesystem::path socketPath_;
    int                   listenFd_ = -1;

    // Main-thread only.
    std::unordered_map<ConnectionId, std::unique_ptr<Connection>> connections_;
    struct RemotePeer {
        std::string                      host;
        std::function<void(std::string)> send;
    };
    std::unordered_map<ConnectionId, RemotePeer>                  remotes_;
    ConnectionId                                                  nextConnectionId_ = 1;

    LifetimeGuard lifetime_;
    std::jthread  acceptThread_;
};

} // namespace ned::editor::mcp

#endif // NED_EDITOR_MCP_BRIDGESERVER_H
