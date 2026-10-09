#include "McpRelays.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <system_error>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ned::editor::host {

namespace {

    constexpr int kListenBacklog = 4;

    std::filesystem::path PrivateDirectory() {
        const char*       runtime = std::getenv("XDG_RUNTIME_DIR");
        const std::string base    = runtime != nullptr && *runtime != '\0' ? runtime : std::filesystem::temp_directory_path().string();
        std::string       pattern = (std::filesystem::path(base) / "ned-agent-mcp-XXXXXX").string();
        if (::mkdtemp(pattern.data()) == nullptr) {
            throw std::runtime_error("ned-agent: can't make a directory for MCP relays: " + std::string(std::strerror(errno)));
        }
        return pattern;
    }

} // namespace

McpRelays::McpRelays(PostQueue& postQueue, Events events) : postQueue_(postQueue), events_(std::move(events)), directory_(PrivateDirectory()) {
    socketPath_                  = directory_ / "mcp.sock";
    const std::string socketPath = socketPath_.string();
    if (socketPath.size() >= sizeof(sockaddr_un{}.sun_path)) {
        std::filesystem::remove(directory_);
        throw std::runtime_error("ned-agent: MCP relay socket path too long: " + socketPath);
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        std::filesystem::remove(directory_);
        throw std::runtime_error(std::string("ned-agent: MCP relay socket() failed: ") + std::strerror(errno));
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socketPath.c_str(), sizeof(address.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(fd, kListenBacklog) != 0) {
        const std::string message = std::string("ned-agent: MCP relay socket: ") + std::strerror(errno);
        ::close(fd);
        std::error_code ec;
        std::filesystem::remove_all(directory_, ec);
        throw std::runtime_error(message);
    }
    listenFd_     = fd;
    acceptThread_ = std::jthread([this, alive = lifetime_.Token()](const std::stop_token& stop) { AcceptLoop(stop, alive); });
}

McpRelays::~McpRelays() {
    lifetime_.Revoke();
    // Woken without freeing the descriptor accept() blocks on, joined, then closed.
    acceptThread_.request_stop();
    ::shutdown(listenFd_, SHUT_RDWR);
    acceptThread_ = std::jthread();
    ::close(listenFd_);
    channels_.clear();
    std::error_code ec;
    std::filesystem::remove_all(directory_, ec);
}

const std::filesystem::path& McpRelays::SocketPath() const noexcept {
    return socketPath_;
}

bool McpRelays::Write(std::uint64_t channel, const std::string& line) {
    const auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return false;
    }
    it->second->SendFrame(line);
    return true;
}

void McpRelays::Close(std::uint64_t channel) {
    channels_.erase(channel);
}

void McpRelays::AcceptLoop(const std::stop_token& stop, const LifetimeToken& alive) {
    while (!stop.stop_requested()) {
        const int connection = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (connection < 0) {
            if (errno == EINTR) {
                continue;
            }
            return; // hung up by the destructor, or a fatal error either way
        }
        if (stop.stop_requested()) {
            ::close(connection);
            return;
        }
        // The socket is both ends; a dup keeps the transport from closing one fd twice.
        const int writeFd = ::dup(connection);
        if (writeFd < 0) {
            ::close(connection);
            continue;
        }
        std::shared_ptr<protocol::LineTransport> transport;
        try {
            transport = std::make_shared<protocol::LineTransport>(connection, writeFd, -1);
        }
        catch (const std::exception&) {
            continue; // LineTransport released both fds
        }
        postQueue_.Post(alive.Bind([this, transport] { Adopt(std::move(*transport)); }));
    }
}

void McpRelays::Adopt(protocol::LineTransport transport) {
    const std::uint64_t channel    = ++nextChannel_;
    auto                connection = std::make_unique<Channel>(std::move(transport), postQueue_, Channel::Options{.entityNoun = "MCP relay"});
    connection->SetOnFrame([this, channel](const std::string& line) {
        if (events_.line) {
            events_.line(channel, line);
        }
    });
    connection->SetOnDisconnected([this, channel](const std::string&) {
        // Deferred: erasing here would destroy the connection from inside
        // its own callback.
        postQueue_.Post(lifetime_.Bind([this, channel] {
            if (channels_.erase(channel) != 0 && events_.closed) {
                events_.closed(channel);
            }
        }));
    });
    channels_.emplace(channel, std::move(connection));
    if (events_.opened) {
        events_.opened(channel);
    }
}

} // namespace ned::editor::host
