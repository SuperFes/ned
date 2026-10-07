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
#include <functional>
#include <optional>
#include <set>
#include <vector>

#include "Repo.h"
#include "RowStatus.h"

namespace ned::text {
class BufferList;
} // namespace ned::text

namespace ned::editor::vcs {

// Absolute, lexically-normalized paths of every entry in `sections` that is
// both VCS-unmerged and still carries real conflict markers on disk. Untracked
// files are never checked -- "conflict" is a merge concept, and `git status`
// never marks an untracked path unmerged in the first place.
[[nodiscard]] std::set<std::filesystem::path> DetectConflictedFiles(const StatusSections& sections,
                                                                    const std::filesystem::path& root);

// The absolute, lexically-normalized paths `sections` lists unmerged, each
// once -- what a marker check (DetectConflictedFiles, or
// Runner::RequestMarkedFiles for a host's) narrows.
[[nodiscard]] std::vector<std::filesystem::path> UnmergedPaths(const StatusSections& sections, const std::filesystem::path& root);

// Whether the buffer holding path (on repo's machine) carries markers;
// nullopt when none is open. Runner::RequestMarkedFiles' `live`, called
// only before that returns.
[[nodiscard]] std::function<std::optional<bool>(const std::filesystem::path&)> OpenBufferMarkers(text::BufferList& buffers,
                                                                                                 const Repo&       repo);

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_CONFLICTEDFILES_H
