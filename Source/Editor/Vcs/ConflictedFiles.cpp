#include "ConflictedFiles.h"

#include <vector>

#include "Sequence.h"

namespace ned::editor::vcs {

std::set<std::filesystem::path> DetectConflictedFiles(const StatusSections& sections, const std::filesystem::path& root) {
    std::set<std::filesystem::path> conflicted;
    const auto                      scan = [&](const std::vector<StatusEntry>& entries) {
        for (const StatusEntry& entry : entries) {
            if (!IsUnmergedStatus(entry.state)) {
                continue;
            }
            const std::filesystem::path absPath = (root / entry.path).lexically_normal();
            if (FileHasConflictMarkers(absPath)) {
                conflicted.insert(absPath);
            }
        }
    };
    scan(sections.staged);
    scan(sections.unstaged);
    return conflicted;
}

} // namespace ned::editor::vcs
