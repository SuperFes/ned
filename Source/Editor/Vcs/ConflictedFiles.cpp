#include "ConflictedFiles.h"

#include <vector>

#include "Sequence.h"
#include "Text/BufferList.h"

namespace ned::editor::vcs {

std::vector<std::filesystem::path> UnmergedPaths(const StatusSections& sections, const std::filesystem::path& root) {
    std::set<std::filesystem::path> unmerged;
    for (const std::vector<StatusEntry>* entries : {&sections.staged, &sections.unstaged}) {
        for (const StatusEntry& entry : *entries) {
            if (IsUnmergedStatus(entry.state)) {
                unmerged.insert((root / entry.path).lexically_normal());
            }
        }
    }
    return {unmerged.begin(), unmerged.end()};
}

std::set<std::filesystem::path> DetectConflictedFiles(const StatusSections& sections, const std::filesystem::path& root) {
    std::set<std::filesystem::path> conflicted;
    for (const std::filesystem::path& path : UnmergedPaths(sections, root)) {
        if (FileHasConflictMarkers(path)) {
            conflicted.insert(path);
        }
    }
    return conflicted;
}

std::function<std::optional<bool>(const std::filesystem::path&)> OpenBufferMarkers(text::BufferList& buffers, const Repo& repo) {
    return [&buffers, repo](const std::filesystem::path& path) -> std::optional<bool> {
        const text::Buffer* buffer = repo.IsRemote() ? buffers.FindByRemote(text::RemoteFile{.destination = *repo.destination, .path = path})
                                                     : buffers.FindByPath(path);
        return buffer != nullptr ? std::optional(buffer->HasConflictMarkers()) : std::nullopt;
    };
}

} // namespace ned::editor::vcs
