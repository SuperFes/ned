//
// Project-wide file browser (project-sidebar follow-up): a UI-agnostic
// recursive directory-tree builder, mirroring ProjectSearch.h's shape --
// see Source/UI/ProjectSidebar.h/.cpp for the widget that renders this.
//

#ifndef NED_EDITOR_PROJECTTREE_H
#define NED_EDITOR_PROJECTTREE_H

#include <filesystem>
#include <functional>
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
//
// shouldExpand (project-sidebar-eager-walk follow-up), when set, is
// consulted before descending into each directory: a directory is always
// listed itself, but its children are only walked if shouldExpand(path)
// returns true. Left unset (the default), every directory is walked --
// the original, unconditional-full-walk behavior every pre-existing caller
// (ProjectAgenda's ".org" scan, BufferView's project-find-file candidate
// list, every existing test) still gets. ProjectSidebar is the one caller
// that passes a real predicate, checking its own expandedDirs_ -- there's
// no point recursively walking a collapsed subtree the sidebar isn't even
// going to show, and for a root with no VCS marker (falls back to the
// opened file's whole containing directory -- see ProjectRoot.h) that
// unconditional walk could mean the entire $HOME tree, repeated every
// kTreeCacheThrottle window.
[[nodiscard]] std::vector<ProjectTreeEntry> BuildProjectTree(
    const std::filesystem::path& root, const std::function<bool(const std::filesystem::path&)>& shouldExpand = nullptr);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTTREE_H
