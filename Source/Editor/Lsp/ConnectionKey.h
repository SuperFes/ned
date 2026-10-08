//
// Which server process a buffer's requests go to: one per server, root and
// host. The project root here keeps the bare server key, another local root
// is "<root>\x1f<server>", and a root on a host is
// "\x1e<destination>\x1e<root>\x1f<server>", which no local key can be.
//

#ifndef NED_EDITOR_LSP_CONNECTIONKEY_H
#define NED_EDITOR_LSP_CONNECTIONKEY_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor::lsp {

// The root a server is started for, and the host it runs on when the root
// is that host's path.
struct ServerRoot {
    std::optional<std::string> host;
    std::filesystem::path      path;

    [[nodiscard]] bool operator==(const ServerRoot&) const = default;
};

// projectRoot only matters for a local root.
[[nodiscard]] std::string MakeConnectionKey(const ServerRoot& root, const std::filesystem::path& projectRoot, std::string_view serverKey);

// The host a key's server runs on; nullopt for a local one.
[[nodiscard]] std::optional<std::string> ConnectionHost(std::string_view key);

[[nodiscard]] std::string_view ConnectionServer(std::string_view key);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_CONNECTIONKEY_H
