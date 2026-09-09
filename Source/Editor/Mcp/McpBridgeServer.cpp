#include "McpBridgeServer.h"

#include <cerrno>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <system_error>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "McpSocketPath.h"
#include "McpToolRegistry.h"

namespace ned::editor::mcp {

namespace {
    constexpr int kListenBacklog = 1; // only one relay is ever expected to connect at a time -- see this file's own header comment
} // namespace

McpBridgeServer::McpBridgeServer(ToolRegistry& registry, ned::ui::EventLoop& eventLoop) : registry_(registry), eventLoop_(eventLoop) {
}

McpBridgeServer::~McpBridgeServer() {
    *alive_ = false;
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (const int connFd = currentConnFd_.load(); connFd >= 0) {
        ::shutdown(connFd, SHUT_RDWR);
    }
    // acceptThread_'s own destructor (below, runs next) requests stop and
    // joins -- the shutdown() calls above are what actually unblock its
    // blocking accept()/read() so that join doesn't hang.
    std::error_code ec;
    if (!socketPath_.empty()) {
        std::filesystem::remove(socketPath_, ec); // best-effort; a stale socket file left behind is harmless (Start() rebinds over it next time)
    }
}

void McpBridgeServer::Start() {
    if (listenFd_ >= 0) {
        return;
    }
    EnsureMcpRuntimeDirectory();
    socketPath_ = McpSocketPathForPid(::getpid());

    const std::string socketPathStr = socketPath_.string();
    if (socketPathStr.size() >= sizeof(sockaddr_un{}.sun_path)) {
        throw std::runtime_error("ned: MCP bridge socket path too long: " + socketPathStr);
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("ned: MCP bridge socket() failed: ") + std::strerror(errno));
    }

    std::error_code removeEc;
    std::filesystem::remove(socketPath_, removeEc); // clear a stale socket file from an earlier crashed instance reusing this pid

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPathStr.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const std::string message = std::string("ned: MCP bridge bind() failed: ") + std::strerror(errno);
        ::close(fd);
        throw std::runtime_error(message);
    }
    if (::listen(fd, kListenBacklog) != 0) {
        const std::string message = std::string("ned: MCP bridge listen() failed: ") + std::strerror(errno);
        ::close(fd);
        throw std::runtime_error(message);
    }

    listenFd_     = fd;
    acceptThread_ = std::jthread([this](std::stop_token stopToken) { AcceptLoop(stopToken); });
}

bool McpBridgeServer::IsListening() const noexcept {
    return listenFd_ >= 0;
}

const std::filesystem::path& McpBridgeServer::SocketPath() const noexcept {
    return socketPath_;
}

void McpBridgeServer::AcceptLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        const int connFd = ::accept(listenFd_, nullptr, nullptr);
        if (connFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return; // listenFd_ was closed out from under us (destructor tearing down) or a genuine fatal error either way -- exit the loop
        }
        currentConnFd_.store(connFd);
        // A single accept()ed socket fd is used as both ends -- mirroring
        // `ned --lsp-broker-stop`'s own dup()-based one-shot socket write in
        // main.cpp -- since ChildProcess's destructor shuts down/closes
        // readFd_ and writeFd_ independently and would otherwise
        // double-close one fd number if they were identical.
        const int dupFd = ::dup(connFd);
        if (dupFd < 0) {
            ::close(connFd);
            currentConnFd_.store(-1);
            continue;
        }
        auto transport = std::make_shared<Transport>(connFd, dupFd, -1);
        ServeConnection(transport, stopToken);
        currentConnFd_.store(-1);
    }
}

void McpBridgeServer::ServeConnection(const std::shared_ptr<Transport>& transport, std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        std::optional<std::string> line;
        try {
            line = transport->ReadMessage();
        }
        catch (const std::exception&) {
            return; // a stalled/malformed peer -- same as a clean disconnect from this loop's point of view
        }
        if (!line) {
            return; // EOF: the relay subprocess exited (agent tore down its MCP server, or the ACP session ended)
        }
        if (line->empty()) {
            continue; // a blank line -- tolerate the same way Acp::Transport's own ReadMessage documents
        }
        std::shared_ptr<bool> aliveCopy = alive_;
        std::string           frameLine = *line;
        eventLoop_.Post([this, transport, frameLine, aliveCopy] {
            if (!*aliveCopy) {
                return;
            }
            HandleFrame(transport, frameLine);
        });
    }
}

void McpBridgeServer::HandleFrame(const std::shared_ptr<Transport>& transport, const std::string& line) {
    Json frame;
    try {
        frame = Json::parse(line);
    }
    catch (const Json::parse_error&) {
        return; // malformed JSON with no reliable id to answer -- nothing sensible to send back
    }

    const Json        id     = frame.value("id", Json());
    const bool        hasId  = frame.contains("id");
    const std::string method = frame.value("method", std::string());

    if (method == "initialize") {
        const Json&       params          = frame.value("params", Json::object());
        const std::string protocolVersion = params.value("protocolVersion", std::string("2025-06-18"));
        SendResult(transport, id,
                   Json{
                       {"protocolVersion", protocolVersion},
                       {"capabilities", {{"tools", Json::object()}}},
                       {"serverInfo", {{"name", "ned"}, {"version", "1"}}},
                   });
        return;
    }
    if (method == "notifications/initialized" || method == "initialized") {
        return; // a notification -- no response expected or sent
    }
    if (method == "tools/list") {
        Json tools = Json::array();
        for (const ToolDescriptor& tool : registry_.ListTools()) {
            tools.push_back(Json{{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}});
        }
        SendResult(transport, id, Json{{"tools", tools}});
        return;
    }
    if (method == "tools/call") {
        const Json&       params = frame.value("params", Json::object());
        const std::string name   = params.value("name", std::string());
        const Json        args   = params.value("arguments", Json::object());
        if (!registry_.HasTool(name)) {
            SendError(transport, id, -32601, "Unknown tool: " + name);
            return;
        }
        std::weak_ptr<Transport> weakTransport = transport;
        std::shared_ptr<bool>    aliveCopy     = alive_;
        registry_.CallTool(name, args, [this, weakTransport, id, aliveCopy](Json toolResult) {
            if (!*aliveCopy) {
                return;
            }
            SendResult(weakTransport, id, toolResult);
        });
        return;
    }
    if (hasId) {
        SendError(transport, id, -32601, "Method not found: " + method);
    }
}

void McpBridgeServer::SendResult(const std::weak_ptr<Transport>& transport, const Json& id, const Json& result) {
    const std::shared_ptr<Transport> locked = transport.lock();
    if (!locked || id.is_null()) {
        return; // connection dropped, or this was a notification (no id to answer)
    }
    try {
        locked->WriteMessage(Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump());
    }
    catch (const std::exception&) {
        // the relay disconnected between the request and this response -- nothing more to do
    }
}

void McpBridgeServer::SendError(const std::weak_ptr<Transport>& transport, const Json& id, int code, const std::string& message) {
    const std::shared_ptr<Transport> locked = transport.lock();
    if (!locked || id.is_null()) {
        return;
    }
    try {
        locked->WriteMessage(Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}}.dump());
    }
    catch (const std::exception&) {
    }
}

} // namespace ned::editor::mcp
