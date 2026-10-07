//
// Completing a host's path in a prompt: what's typed, split into the
// directory to list there and the name begun in it, and a listing turned
// into the candidates the prompt offers.
//

#ifndef NED_EDITOR_REMOTE_PATHCOMPLETION_H
#define NED_EDITOR_REMOTE_PATHCOMPLETION_H

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Host/FsCodec.h"
#include "Text/RemoteFile.h"

namespace ned::editor::remote {

struct RemotePathPrefix {
    text::RemoteFile directory;
    std::string      typedDirectory; // as typed, through its last '/'
    std::string      partialName;
};

// nullopt unless typed is a destination, ':', then an absolute path.
[[nodiscard]] std::optional<RemotePathPrefix> SplitRemotePathPrefix(std::string_view typed);

// The entries that begin prefix's partial name, each as the prompt would
// read it, a directory's with a trailing '/', sorted. directoriesOnly
// leaves out everything else.
[[nodiscard]] std::vector<std::string> CompleteRemotePath(const RemotePathPrefix& prefix, std::span<const host::DirEntry> entries,
                                                          bool directoriesOnly = false);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_PATHCOMPLETION_H
