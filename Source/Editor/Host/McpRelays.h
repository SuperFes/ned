//
// The host half of ned's MCP tools for an agent on a host: a private Unix
// socket the agent's MCP server (`ned-agent --mcp-relay <socket>`) connects
// to, each connection a channel whose newline-delimited JSON the
// dispatcher carries to the editor and back.
//
//   mcp/listen    params {}                    result {path}
//   $/mcp/opened  {channel}                    host -> editor
//   $/mcp/line    {channel, line: bytes}       host -> editor
//   $/mcp/closed  {channel}                    host -> editor
//   mcp/write     params {channel, line: bytes} result {}
//   mcp/close     params {channel}              result {}
//
// The socket lives in a directory of its own, mode 0700, under
// $XDG_RUNTIME_DIR (or the system's temporary directory); both go when
// this does.
//

#ifndef NED_EDITOR_HOST_MCPRELAYS_H
#define NED_EDITOR_HOST_MCPRELAYS_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Protocol/FramedConnection.h"
#include "Editor/Protocol/LineTransport.h"

namespace ned::editor::host {

inline constexpr std::string_view kMcpListen = "mcp/listen";
inline constexpr std::string_view kMcpWrite  = "mcp/write";
inline constexpr std::string_view kMcpClose  = "mcp/close";
inline constexpr std::string_view kMcpOpened = "$/mcp/opened";
inline constexpr std::string_view kMcpLine   = "$/mcp/line";
inline constexpr std::string_view kMcpClosed = "$/mcp/closed";

class McpRelays {
  public:
    struct Events {
        std::function<void(std::uint64_t channel)>                          opened;
        std::function<void(std::uint64_t channel, const std::string& line)> line;
        std::function<void(std::uint64_t channel)>                          closed;
    };

    // Listens at once; throws std::runtime_error when it can't. Every event
    // runs on postQueue's thread.
    McpRelays(PostQueue& postQueue, Events events);
    // Stops listening, hangs up every channel, and removes the socket.
    ~McpRelays();

    McpRelays(const McpRelays&)            = delete;
    McpRelays& operator=(const McpRelays&) = delete;

    [[nodiscard]] const std::filesystem::path& SocketPath() const noexcept;

    // False for a channel that has closed.
    bool Write(std::uint64_t channel, const std::string& line);
    void Close(std::uint64_t channel);

  private:
    using Channel = protocol::FramedConnection<protocol::LineTransport>;

    void AcceptLoop(const std::stop_token& stop, const LifetimeToken& alive);
    void Adopt(protocol::LineTransport transport);

    PostQueue&                                                  postQueue_;
    Events                                                      events_;
    std::filesystem::path                                       directory_;
    std::filesystem::path                                       socketPath_;
    int                                                         listenFd_ = -1;
    std::unordered_map<std::uint64_t, std::unique_ptr<Channel>> channels_;
    std::uint64_t                                               nextChannel_ = 0;
    LifetimeGuard                                               lifetime_;
    std::jthread                                                acceptThread_;
};

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_MCPRELAYS_H
