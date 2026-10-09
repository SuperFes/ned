#include "BridgeServer.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "SocketPath.h"
#include "Text/RemoteFile.h"
#include "ToolRegistry.h"

namespace ned::editor::mcp {

namespace {
    constexpr int kListenBacklog = 4; // accept() never waits on a served relay, so this only holds relays connecting at the same instant
} // namespace

BridgeServer::BridgeServer(ToolRegistry& registry, ned::ui::EventLoop& eventLoop) : registry_(registry), eventLoop_(eventLoop) {
}

BridgeServer::~BridgeServer() {
    lifetime_.Revoke();
    // Wake accept() without freeing the descriptor it is blocked on, join,
    // and only then close. The stop is requested first: a connection
    // accepted after the hang-up below sees it and goes away.
    acceptThread_.request_stop();
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
    }
    acceptThread_ = std::jthread();
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    connections_.clear();
    std::error_code ec;
    if (!socketPath_.empty()) {
        std::filesystem::remove(socketPath_, ec); // best-effort; a stale socket file left behind is harmless (Start() rebinds over it next time)
    }
}

void BridgeServer::Start() {
    if (listenFd_ >= 0) {
        return;
    }
    EnsureRuntimeDirectory();
    socketPath_ = SocketPathForPid(::getpid());

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
    acceptThread_ = std::jthread([this, alive = lifetime_.Token()](const std::stop_token& stopToken) { AcceptLoop(stopToken, alive); });
}

bool BridgeServer::IsListening() const noexcept {
    return listenFd_ >= 0;
}

const std::filesystem::path& BridgeServer::SocketPath() const noexcept {
    return socketPath_;
}

void BridgeServer::AcceptLoop(const std::stop_token& stopToken, const LifetimeToken& alive) {
    while (!stopToken.stop_requested()) {
        const int connFd = ::accept(listenFd_, nullptr, nullptr);
        if (connFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return; // listenFd_ was shut down (destructor tearing down) or a genuine fatal error either way -- exit the loop
        }
        if (stopToken.stop_requested()) {
            ::close(connFd);
            return;
        }
        // A single accept()ed socket fd is used as both ends -- mirroring
        // `ned --lsp-broker-stop`'s own dup()-based one-shot socket write in
        // main.cpp -- since ChildProcess's destructor shuts down/closes
        // readFd_ and writeFd_ independently and would otherwise
        // double-close one fd number if they were identical.
        const int dupFd = ::dup(connFd);
        if (dupFd < 0) {
            ::close(connFd);
            continue;
        }
        std::shared_ptr<protocol::LineTransport> transport;
        try {
            transport = std::make_shared<protocol::LineTransport>(connFd, dupFd, -1);
        }
        catch (const std::exception&) {
            continue; // LineTransport released both fds
        }
        // Shared, so a post dropped by teardown still closes the socket.
        eventLoop_.Post(alive.Bind([this, transport] { Adopt(std::move(*transport)); }));
    }
}

void BridgeServer::Adopt(protocol::LineTransport transport) {
    const ConnectionId id         = nextConnectionId_++;
    auto               connection = std::make_unique<Connection>(
        std::move(transport), eventLoop_,
        Connection::Options{.logCategory = LogCategory::Acp, .entityNoun = "MCP relay", .disconnectSeverity = LogSeverity::Info});
    connection->SetOnFrame([this, id](const std::string& frame) { HandleFrame(id, frame); });
    connection->SetOnDisconnected([this, id](const std::string&) {
        // Deferred: erasing here would destroy the connection from inside
        // its own callback.
        eventLoop_.Post(lifetime_.Bind([this, id] { connections_.erase(id); }));
    });
    connections_.emplace(id, std::move(connection));
}

std::uint64_t BridgeServer::AdoptRemote(std::string host, std::function<void(std::string line)> send) {
    const ConnectionId id = nextConnectionId_++;
    remotes_.emplace(id, RemotePeer{.host = std::move(host), .send = std::move(send)});
    return id;
}

void BridgeServer::ReceiveRemote(std::uint64_t id, const std::string& line) {
    if (remotes_.contains(id)) {
        HandleFrame(id, line);
    }
}

void BridgeServer::CloseRemote(std::uint64_t id) {
    remotes_.erase(id);
}

void BridgeServer::HandleFrame(ConnectionId connectionId, const std::string& line) {
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
        SendResult(connectionId, id,
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
        SendResult(connectionId, id, Json{{"tools", tools}});
        return;
    }
    if (method == "tools/call") {
        const Json&       params = frame.value("params", Json::object());
        const std::string name   = params.value("name", std::string());
        Json              args   = params.value("arguments", Json::object());
        // A host's agent names its own files.
        if (const auto remote = remotes_.find(connectionId); remote != remotes_.end() && args.is_object() && args.contains("file") && args["file"].is_string()) {
            if (const std::string file = args["file"].get<std::string>(); file.starts_with('/')) {
                args["file"] = text::RemoteFile{.destination = remote->second.host, .path = std::filesystem::path(file).lexically_normal()}.ToString();
            }
        }
        if (!registry_.HasTool(name)) {
            SendError(connectionId, id, -32601, "Unknown tool: " + name);
            return;
        }
        registry_.CallTool(name, args, lifetime_.Bind([this, connectionId, id](const Json& toolResult) { SendResult(connectionId, id, toolResult); }));
        return;
    }
    if (hasId) {
        SendError(connectionId, id, -32601, "Method not found: " + method);
    }
}

void BridgeServer::SendResult(ConnectionId id, const Json& requestId, const Json& result) {
    Send(id, requestId, Json{{"result", result}});
}

void BridgeServer::SendError(ConnectionId id, const Json& requestId, int code, const std::string& message) {
    Send(id, requestId, Json{{"error", {{"code", code}, {"message", message}}}});
}

void BridgeServer::Send(ConnectionId id, const Json& requestId, Json response) {
    if (requestId.is_null()) {
        return;
    }
    response["jsonrpc"] = "2.0";
    response["id"]      = requestId;
    if (const auto remote = remotes_.find(id); remote != remotes_.end()) {
        remote->second.send(response.dump());
        return;
    }
    const auto connection = connections_.find(id);
    if (connection != connections_.end()) {
        connection->second->SendFrame(response.dump());
    }
}

} // namespace ned::editor::mcp
