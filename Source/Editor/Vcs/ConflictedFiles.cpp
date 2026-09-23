#include "ConflictedFiles.h"

#include <fstream>
#include <iterator>
#include <vector>

#include "Text/ThreeWayMerge.h"

namespace ned::editor::vcs {

std::set<std::filesystem::path> DetectConflictedFiles(const StatusSections& sections, const std::filesystem::path& root) {
    std::set<std::filesystem::path> conflicted;
    const auto                      scan = [&](const std::vector<StatusEntry>& entries) {
        for (const StatusEntry& entry : entries) {
            if (!IsUnmergedStatus(entry.state)) {
                continue;
            }
            const std::filesystem::path absPath = (root / entry.path).lexically_normal();
            std::ifstream                file(absPath, std::ios::binary);
            if (!file) {
                continue;
            }
            const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (text::HasConflictMarkers(content)) {
                conflicted.insert(absPath);
            }
        }
    };
    scan(sections.staged);
    scan(sections.unstaged);
    return conflicted;
}

} // namespace ned::editor::vcs
