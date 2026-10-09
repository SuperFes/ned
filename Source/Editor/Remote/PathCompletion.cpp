#include "PathCompletion.h"

#include <algorithm>

namespace ned::editor::remote {

std::optional<RemotePathPrefix> SplitRemotePathPrefix(std::string_view typed) {
    // "ssh:/" is a URI on its way, not a host named ssh.
    if (text::kSshScheme.starts_with(typed) || !text::ParseRemoteFile(typed)) {
        return std::nullopt;
    }
    const std::size_t               lastSlash = typed.rfind('/');
    std::optional<text::RemoteFile> directory = text::ParseRemoteFile(typed.substr(0, lastSlash + 1));
    if (!directory) {
        return std::nullopt;
    }
    return RemotePathPrefix{.directory      = std::move(*directory),
                            .typedDirectory = std::string(typed.substr(0, lastSlash + 1)),
                            .partialName    = std::string(typed.substr(lastSlash + 1))};
}

std::vector<std::string> CompleteRemotePath(const RemotePathPrefix& prefix, std::span<const host::DirEntry> entries, bool directoriesOnly) {
    std::vector<std::string> matches;
    for (const host::DirEntry& entry : entries) {
        const bool directory = entry.kind == host::FileKind::Directory;
        if ((directoriesOnly && !directory) || !entry.name.starts_with(prefix.partialName)) {
            continue;
        }
        matches.push_back(prefix.typedDirectory + entry.name + (directory ? "/" : ""));
    }
    std::ranges::sort(matches);
    return matches;
}

} // namespace ned::editor::remote
