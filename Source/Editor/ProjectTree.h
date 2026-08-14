//
// Project-wide file browser (project-sidebar follow-up): a UI-agnostic
// recursive directory-tree builder, mirroring ProjectSearch.h's shape --
// see Source/UI/ProjectSidebar.h/.cpp for the widget that renders this.
//

#ifndef NED_EDITOR_PROJECTTREE_H
#define NED_EDITOR_PROJECTTREE_H

#include <filesystem>
#include <vector>

namespace ned::editor {

struct ProjectTreeEntry {
    std::filesystem::path path;  // always absolute, regardless of root's form
    int                   depth; // 0 for root's immediate children
    bool                  isDirectory;
};

// Recursively lists every entry under root, skipping dot-directories (.git,
// .svn, .idea, ...) entirely -- the same rule ProjectSearch's SearchDirectory
// already applies. Order is depth-first (a directory's children immediately
// follow it, not deferred to the end of the whole walk) and, within each
// directory, directories are listed before files, each group sorted
// alphabetically -- the conventional file-browser ordering, matching what
// `ls -la`-style tools and virtually every GUI file browser already do.
// Returns an empty list rather than throwing for a nonexistent/unlistable
// root.
[[nodiscard]] std::vector<ProjectTreeEntry> BuildProjectTree(const std::filesystem::path& root);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTTREE_H
