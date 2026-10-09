#include "FileTreeVcs.h"

namespace ned::ui {

using editor::vcs::RowStatus;

VcsStatusIndex BuildVcsStatusIndex(const std::vector<editor::vcs::StatusEntry>& entries, const std::filesystem::path& root,
                                   const std::set<std::filesystem::path>& conflicted) {
    VcsStatusIndex index;
    const auto     merge = [&index](const std::filesystem::path& path, RowStatus status) {
        auto [it, inserted] = index.try_emplace(path, status);
        if (!inserted && status > it->second) {
            it->second = status;
        }
    };
    for (const editor::vcs::StatusEntry& entry : entries) {
        const std::filesystem::path filePath = (root / entry.path).lexically_normal();
        const RowStatus             status   = conflicted.contains(filePath) ? RowStatus::Conflicted : editor::vcs::ClassifyPorcelainStatus(entry.state);
        merge(filePath, status);

        std::filesystem::path dir = filePath.parent_path();
        while (dir != root) {
            const std::filesystem::path parent = dir.parent_path();
            if (parent == dir) {
                break; // reached the filesystem root without finding root -- not under it
            }
            merge(dir, status);
            dir = parent;
        }
    }
    return index;
}

RowStatus LookupVcsStatus(const VcsStatusIndex& index, const std::filesystem::path& path) {
    const auto it = index.find(path.lexically_normal());
    return it == index.end() ? RowStatus::None : it->second;
}

std::optional<Color> VcsStatusColor(RowStatus status, const Theme& theme) {
    switch (status) {
        case RowStatus::Conflicted:
        case RowStatus::Deleted:
            return theme.diagnosticError;
        case RowStatus::Modified:
            return theme.vcsModifiedForeground;
        case RowStatus::Added:
            return theme.successForeground;
        case RowStatus::Untracked:
            return theme.vcsUntrackedForeground;
        case RowStatus::None:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace ned::ui
