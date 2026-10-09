//
// Remote files and directories named on ned's command line.
//

#ifndef NED_EDITOR_REMOTE_COMMANDLINE_H
#define NED_EDITOR_REMOTE_COMMANDLINE_H

#include <optional>
#include <string_view>

#include "Text/RemoteFile.h"

namespace ned::editor::remote {

// argument as destination:/path, read the way scp reads one: a colon ahead
// of any slash, and nothing here by that name. ./host:/x names a local file.
[[nodiscard]] std::optional<text::RemoteFile> RemoteArgument(std::string_view argument);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_COMMANDLINE_H
