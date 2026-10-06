//
// A place on a remote host: Text/RemoteFile.h's, under the name the remote
// commands use for it.
//

#ifndef NED_EDITOR_REMOTE_LOCATION_H
#define NED_EDITOR_REMOTE_LOCATION_H

#include <optional>
#include <string_view>

#include "Text/RemoteFile.h"

namespace ned::editor::remote {

using Location = text::RemoteFile;

[[nodiscard]] inline std::optional<Location> ParseLocation(std::string_view text) {
    return text::ParseRemoteFile(text);
}

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_LOCATION_H
