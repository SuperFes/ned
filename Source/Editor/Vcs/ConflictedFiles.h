//
// Which working-tree files are a *real* merge conflict, as opposed to files
// merely mentioned by `git status` as unmerged (RowStatus.h's IsUnmergedStatus)
// or merely *about* conflict markers on disk (Text/ThreeWayMerge.h's
// HasConflictMarkers). Both halves are load-bearing -- the status code is what
// separates a real conflict from a file that just happens to contain
// "<<<<<<<" text, and the on-disk scan is what drops an unmerged path already
// resolved in the worktree but not yet staged.
//
// Unlike RowStatus.h (deliberately pure, no filesystem access -- see its own
// header comment), this reads file content, so it belongs in its own header
// rather than blurring that line. First written for VcsPanel's own conflict-
// file affordance, hoisted out here so ProjectSidebar's file-tree highlight
// can share the exact same verdict rather than re-deriving it.
//

#ifndef NED_EDITOR_VCS_CONFLICTEDFILES_H
#define NED_EDITOR_VCS_CONFLICTEDFILES_H

#include <filesystem>
#include <set>

#include "RowStatus.h"

namespace ned::editor::vcs {

// Absolute, lexically-normalized paths of every entry in `sections` that is
// both VCS-unmerged and still carries real conflict markers on disk. Untracked
// files are never checked -- "conflict" is a merge concept, and `git status`
// never marks an untracked path unmerged in the first place.
[[nodiscard]] std::set<std::filesystem::path> DetectConflictedFiles(const StatusSections& sections,
                                                                    const std::filesystem::path& root);

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_CONFLICTEDFILES_H
