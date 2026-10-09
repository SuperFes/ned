#include "RemoteFile.h"

#include <algorithm>

namespace ned::text {

namespace {

    struct Authority {
        std::string_view user; // with its '@'
        std::string_view host; // an IPv6 address keeps its brackets
        std::string_view port;
    };

    Authority SplitAuthority(std::string_view authority) {
        Authority split;
        if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
            split.user = authority.substr(0, at + 1);
            authority.remove_prefix(at + 1);
        }
        // An IPv6 address's colons are inside its brackets.
        const std::size_t hostEnd = authority.starts_with('[') ? authority.find(']') : 0;
        const std::size_t colon   = hostEnd == std::string_view::npos ? hostEnd : authority.find(':', hostEnd);
        split.host                = authority.substr(0, colon);
        if (colon != std::string_view::npos) {
            split.port = authority.substr(colon + 1);
        }
        return split;
    }

    std::filesystem::path NormalPath(std::string_view text) {
        std::filesystem::path path = std::filesystem::path(text).lexically_normal();
        if (!path.has_filename() && path != path.root_path()) {
            path = path.parent_path();
        }
        return path;
    }

} // namespace

std::string RemoteFile::ToString() const {
    // A URI's path follows its authority directly.
    return destination.starts_with(kSshScheme) ? destination + path.string() : destination + ":" + path.string();
}

const std::filesystem::path& PathOf(const FileRef& file) {
    const RemoteFile* remote = std::get_if<RemoteFile>(&file);
    return remote != nullptr ? remote->path : std::get<std::filesystem::path>(file);
}

std::string ToString(const FileRef& file) {
    const RemoteFile* remote = std::get_if<RemoteFile>(&file);
    return remote != nullptr ? remote->ToString() : std::get<std::filesystem::path>(file).string();
}

std::optional<RemoteFile> ParseRemoteFile(std::string_view text) {
    if (text.starts_with(kSshScheme)) {
        const std::string_view rest  = text.substr(kSshScheme.size());
        const std::size_t      slash = rest.find('/');
        if (slash == std::string_view::npos) {
            return std::nullopt;
        }
        std::string_view authority = rest.substr(0, slash);
        if (authority.ends_with(':')) {
            authority.remove_suffix(1);
        }
        if (SplitAuthority(authority).host.empty()) {
            return std::nullopt;
        }
        return RemoteFile{.destination = NormalizeDestination(std::string(kSshScheme) + std::string(authority)), .path = NormalPath(rest.substr(slash))};
    }
    const std::size_t split = text.find(":/");
    if (split == std::string_view::npos || split == 0) {
        return std::nullopt;
    }
    return RemoteFile{.destination = std::string(text.substr(0, split)), .path = NormalPath(text.substr(split + 1))};
}

std::string NormalizeDestination(std::string_view destination) {
    if (!destination.starts_with(kSshScheme)) {
        return std::string(destination);
    }
    std::string_view authority = destination.substr(kSshScheme.size());
    while (authority.ends_with('/')) {
        authority.remove_suffix(1);
    }
    const Authority split = SplitAuthority(authority);
    if (split.port.empty() || split.port == "22") {
        return std::string(split.user) + std::string(split.host);
    }
    return std::string(kSshScheme) + std::string(authority);
}

std::string ShortDestination(std::string_view destination) {
    std::string_view port;
    if (destination.starts_with(kSshScheme)) {
        const Authority split = SplitAuthority(destination.substr(kSshScheme.size()));
        destination           = split.host;
        port                  = split.port;
    }
    else if (const std::size_t at = destination.rfind('@'); at != std::string_view::npos) {
        destination.remove_prefix(at + 1);
    }
    const bool address = destination.starts_with('[') ||
                         std::ranges::all_of(destination, [](char c) { return (c >= '0' && c <= '9') || c == '.'; });
    if (!address) {
        destination = destination.substr(0, destination.find('.'));
    }
    return port.empty() ? std::string(destination) : std::string(destination) + ":" + std::string(port);
}

std::string RemoteBufferName(const RemoteFile& file) {
    return ShortDestination(file.destination) + ":" + file.path.filename().string();
}

} // namespace ned::text
