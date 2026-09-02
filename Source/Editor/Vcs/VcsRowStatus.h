//
// VCS side panel follow-up: the row-severity vocabulary originally built
// for ProjectSidebar's changed-files-highlight follow-up (Source/UI/
// ProjectSidebar.h/.cpp), hoisted here so VcsPanel (Source/UI/VcsPanel.h)
// can share it instead of re-deriving the same git-porcelain-code reading.
// Deliberately still a small, git-porcelain-shaped vocabulary, not a fully
// VCS-agnostic one -- see VcsStatusEntry::state's own doc comment
// (VcsProvider.h) for why that's an accepted, already-established
// simplification rather than a new one.
//

#ifndef NED_EDITOR_VCS_VCSROWSTATUS_H
#define NED_EDITOR_VCS_VCSROWSTATUS_H

#include <string>
#include <vector>

#include "VcsProvider.h"

namespace ned::editor::vcs {

// changed-files-highlight follow-up: how severe a file's (or, for a
// ProjectSidebar directory, its most severe descendant's) git status is,
// ordered least-to-most severe so merging two children's statuses is a
// plain std::max. Deliberately just these four buckets, not a verbatim
// porcelain code -- a row only needs to know which color to paint, the same
// "don't reinterpret VCS-specific text beyond what the UI needs" call
// BufferView's own DiffLineKind already makes for the per-line diff gutter.
enum class VcsRowStatus { None, Untracked, Added, Modified, Deleted };

// Classifies git's own two-letter porcelain "XY" status code
// (VcsStatusEntry::state, kept verbatim by VcsProvider::ParseStatus -- see
// that struct's own comment) into the four buckets above. Checked by
// substring rather than fixed column position: "??" is untracked outright,
// and otherwise either column (index vs. worktree) can carry the letter
// that matters, e.g. "AM" is a staged-then-further-edited add. Priority
// among the remaining letters -- D beats M beats A -- mirrors
// VcsRowStatus's own least-to-most-severe ordering; a letter with no
// dedicated bucket (R rename, C copy, T typechange, U unmerged) falls back
// to Modified, the closest real-world reading of "this file's content
// changed".
[[nodiscard]] VcsRowStatus ClassifyPorcelainStatus(const std::string& state);

// VCS side panel: staged/unstaged/untracked working-tree state, partitioned
// from a flat VcsProvider::ParseStatus result -- the panel's own tree-
// section grouping. Interprets git's two-char "XY" porcelain code the same
// verbatim-text-kept convention ClassifyPorcelainStatus above and
// VcsStatusEntry::state's own doc comment already establish: column 0 (X,
// the index/staged state) is non-space/non-'?' for a staged change, column
// 1 (Y, the worktree/unstaged state) is non-space for an unstaged change --
// "??" (untracked) and both columns set (e.g. "AM", staged then further
// edited) are each handled explicitly below since they don't fit the
// either-column-alone reading. A file with both a staged and an unstaged
// component (e.g. "AM") appears in both sections, matching how git's own
// `status` output and every real git GUI already show that same file twice.
// Pure, no VcsRunner/subprocess involved -- unit-tested directly against
// crafted porcelain strings.
struct VcsStatusSections {
    std::vector<VcsStatusEntry> staged;
    std::vector<VcsStatusEntry> unstaged;
    std::vector<VcsStatusEntry> untracked;
};

[[nodiscard]] VcsStatusSections PartitionVcsStatus(const std::vector<VcsStatusEntry>& entries);

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_VCSROWSTATUS_H
