//
// A place on a remote host: Text/RemoteFile.h's, under the name the remote
// commands use for it.
//

#ifndef NED_EDITOR_REMOTE_LOCATION_H
#define NED_EDITOR_REMOTE_LOCATION_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "Text/RemoteFile.h"

namespace ned::editor::remote {

using Location = text::RemoteFile;

[[nodiscard]] inline std::optional<Location> ParseLocation(std::string_view text) {
    return text::ParseRemoteFile(text);
}

// The host a host:/path prompt holds while no path has begun yet: up to a
// ':', or an ssh URI up to its path's '/'. nullopt once a path has begun.
[[nodiscard]] inline std::optional<std::string> TypedHost(std::string_view text) {
    if (text.starts_with(text::kSshScheme)) {
        const std::string_view authority = text.substr(text::kSshScheme.size());
        if (authority.find('/') != std::string_view::npos) {
            return std::nullopt;
        }
        return std::string(text);
    }
    if (text.find(":/") != std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(text.substr(0, text.find(':')));
}

// The text a host's root path starts after, for completing a host into a
// host:/path prompt.
[[nodiscard]] inline std::string RootOf(const std::string& destination) {
    return destination.starts_with(text::kSshScheme) ? destination + "/" : destination + ":/";
}

// What a path prompt means by text: host:/path, unless a local file has that
// name.
[[nodiscard]] inline std::optional<Location> ParsePromptLocation(std::string_view text) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::symlink_status(std::filesystem::path(text), ec)) ? std::nullopt : ParseLocation(text);
}

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_LOCATION_H
