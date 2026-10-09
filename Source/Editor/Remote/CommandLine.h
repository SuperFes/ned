//
// Remote files and directories named on ned's command line.
//

#ifndef NED_EDITOR_REMOTE_COMMANDLINE_H
#define NED_EDITOR_REMOTE_COMMANDLINE_H

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "Editor/Lifetime.h"
#include "Text/RemoteFile.h"

namespace ned::editor::remote {

class Hosts;

// argument as destination:/path, read the way scp reads one: a colon ahead
// of any slash, and nothing here by that name. ./host:/x names a local file.
[[nodiscard]] std::optional<text::RemoteFile> RemoteArgument(std::string_view argument);

enum class RemoteArgumentKind { Directory,
                                File };

// What argument names on its host, connecting first: a directory, or a file
// to open, which a path the host doesn't have yet counts as. done runs on
// the main thread, and not at all once token has died.
void ClassifyRemoteArgument(Hosts& hosts, const text::RemoteFile& argument, LifetimeToken token,
                            std::function<void(std::expected<RemoteArgumentKind, std::string> kind)> done);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_COMMANDLINE_H
