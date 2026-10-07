//
// Wire forms for the watch namespace (Docs/ClientServerProtocol.md, section 7).
//
//   watch/set      params {files: [path], directories: [path]}
//                  result {}
//                  Replaces everything the connection watches: a file by
//                  its name in its directory, a directory by every entry
//                  directly in it.
//   $/watch/event  host to editor, once per burst of changes
//                  params {events: [event], overflow: bool}
//
//   event          {path, kind: "created" | "changed" | "deleted"}
//
// Events name the paths as watch/set did, symlinks and all. overflow means
// the host lost events, so anything watched may have changed.
//

#ifndef NED_EDITOR_HOST_WATCHCODEC_H
#define NED_EDITOR_HOST_WATCHCODEC_H

#include <filesystem>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/FileWatch.h"

namespace ned::editor::host {

inline constexpr std::string_view kWatchSet   = "watch/set";
inline constexpr std::string_view kWatchEvent = "$/watch/event";

struct WatchSetRequest {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> directories;

    [[nodiscard]] bool operator==(const WatchSetRequest&) const = default;
};

struct WatchEvents {
    std::vector<FileEvent> events;
    bool                   overflow = false;

    [[nodiscard]] bool operator==(const WatchEvents&) const = default;
};

// The decoders throw protocol::ProtocolError on a malformed field.
[[nodiscard]] nlohmann::json  ToJson(const WatchSetRequest& request);
[[nodiscard]] WatchSetRequest WatchSetRequestFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json ToJson(const WatchEvents& events);
[[nodiscard]] WatchEvents    WatchEventsFromJson(const nlohmann::json& json);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_WATCHCODEC_H
