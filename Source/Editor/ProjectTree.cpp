#include "ProjectTree.h"

#include <algorithm>

namespace ned::editor {

namespace {

    // Mirrors ProjectSearch.cpp's own IsDotDirectory exactly -- kept as a
    // separate one-line copy rather than a shared header, since sharing a
    // predicate this small isn't worth a new dependency between the two.
    bool IsDotDirectory(const std::filesystem::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        return !name.empty() && name.front() == '.';
    }

    void WalkTree(const std::filesystem::path& directory, int depth, std::vector<ProjectTreeEntry>& out) {
        std::vector<std::filesystem::path> directories;
        std::vector<std::filesystem::path> files;

        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (entry.is_directory()) {
                if (!IsDotDirectory(entry)) {
                    directories.push_back(entry.path());
                }
            }
            else if (entry.is_regular_file()) {
                files.push_back(entry.path());
            }
        }
        if (ec) {
            return;
        }

        std::sort(directories.begin(), directories.end());
        std::sort(files.begin(), files.end());

        for (const std::filesystem::path& dir : directories) {
            out.push_back(ProjectTreeEntry{dir, depth, true});
            WalkTree(dir, depth + 1, out);
        }
        for (const std::filesystem::path& file : files) {
            out.push_back(ProjectTreeEntry{file, depth, false});
        }
    }

} // namespace

std::vector<ProjectTreeEntry> BuildProjectTree(const std::filesystem::path& root) {
    std::vector<ProjectTreeEntry> entries;

    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) {
        return entries;
    }

    WalkTree(absoluteRoot, 0, entries);
    return entries;
}

} // namespace ned::editor
