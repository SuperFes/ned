//
// A place on a remote host: Text/RemoteFile.h's, under the name the remote
// commands use for it.
//

#ifndef NED_EDITOR_REMOTE_LOCATION_H
#define NED_EDITOR_REMOTE_LOCATION_H

#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

#include "Text/RemoteFile.h"

namespace ned::editor::remote {

using Location = text::RemoteFile;

[[nodiscard]] inline std::optional<Location> ParseLocation(std::string_view text) {
    return text::ParseRemoteFile(text);
}

// What a path prompt means by text: host:/path, unless a local file has that
// name.
[[nodiscard]] inline std::optional<Location> ParsePromptLocation(std::string_view text) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::symlink_status(std::filesystem::path(text), ec)) ? std::nullopt : ParseLocation(text);
}

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_LOCATION_H
