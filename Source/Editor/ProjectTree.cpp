#include "ProjectTree.h"

#include <algorithm>

#include "GitIgnore.h"

namespace ned::editor {

namespace {

    // Mirrors ProjectSearch.cpp's own IsDotDirectory exactly -- kept as a
    // separate one-line copy rather than a shared header, since sharing a
    // predicate this small isn't worth a new dependency between the two.
    bool IsDotDirectory(const std::filesystem::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        return !name.empty() && name.front() == '.';
    }

    // project-search-hang follow-up: absoluteRoot is threaded through
    // purely to compute each entry's root-relative path for
    // gitIgnore.IsIgnored -- see GitIgnore.h's own header comment for why
    // this exists (without it, the sidebar eagerly walks build/,
    // node_modules/, and similar generated/dependency directories too).
    void WalkTree(const std::filesystem::path& directory, const std::filesystem::path& absoluteRoot, const GitIgnoreMatcher& gitIgnore,
                  int depth, const std::function<bool(const std::filesystem::path&)>& shouldExpand, std::vector<ProjectTreeEntry>& out) {
        std::vector<std::filesystem::path> directories;
        std::vector<std::filesystem::path> files;

        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, ec)) {
            const std::filesystem::path relative = std::filesystem::relative(entry.path(), absoluteRoot);
            if (entry.is_directory()) {
                if (!IsDotDirectory(entry) && !gitIgnore.IsIgnored(relative, /*isDirectory=*/true)) {
                    directories.push_back(entry.path());
                }
            }
            else if (entry.is_regular_file() && !gitIgnore.IsIgnored(relative, /*isDirectory=*/false)) {
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
            if (!shouldExpand || shouldExpand(dir)) {
                WalkTree(dir, absoluteRoot, gitIgnore, depth + 1, shouldExpand, out);
            }
        }
        for (const std::filesystem::path& file : files) {
            out.push_back(ProjectTreeEntry{file, depth, false});
        }
    }

} // namespace

std::vector<ProjectTreeEntry> BuildProjectTree(const std::filesystem::path& root,
                                                const std::function<bool(const std::filesystem::path&)>& shouldExpand) {
    std::vector<ProjectTreeEntry> entries;

    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) {
        return entries;
    }

    const GitIgnoreMatcher gitIgnore(absoluteRoot);
    WalkTree(absoluteRoot, absoluteRoot, gitIgnore, 0, shouldExpand, entries);
    return entries;
}

} // namespace ned::editor
