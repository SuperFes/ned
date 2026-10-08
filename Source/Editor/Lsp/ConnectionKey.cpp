#include "ConnectionKey.h"

namespace ned::editor::lsp {

namespace {

    constexpr char kHost   = '\x1e';
    constexpr char kServer = '\x1f';

} // namespace

std::string MakeConnectionKey(const ServerRoot& root, const std::filesystem::path& projectRoot, std::string_view serverKey) {
    if (root.host) {
        return kHost + *root.host + kHost + root.path.string() + kServer + std::string(serverKey);
    }
    if (root.path == projectRoot) {
        return std::string(serverKey);
    }
    return root.path.string() + kServer + std::string(serverKey);
}

std::optional<std::string> ConnectionHost(std::string_view key) {
    if (key.empty() || key.front() != kHost) {
        return std::nullopt;
    }
    const std::size_t end = key.find(kHost, 1);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(key.substr(1, end - 1));
}

std::string_view ConnectionServer(std::string_view key) {
    const std::size_t separator = key.rfind(kServer);
    return separator == std::string_view::npos ? key : key.substr(separator + 1);
}

} // namespace ned::editor::lsp
