//
// Pure unified-diff text manipulation backing hunk-level staging (the
// vocabulary-completion follow-up's named next slice) -- no provider, no
// subprocess, no VCS knowledge beyond the standard unified diff format
// itself, which is exactly why this lives as its own helper instead of
// inside VcsRunner: the patch handed to a provider's apply-patch argv is a
// *verbatim slice* of raw diff output (file header block + one hunk), so
// none of the fiddly content details (`\ No newline at end of file`
// markers, escaped/quoted paths in the header, mode-change lines) ever
// need to be parsed, reconstructed, or understood here -- they pass
// through untouched.
//

#ifndef NED_EDITOR_VCS_DIFFPATCH_H
#define NED_EDITOR_VCS_DIFFPATCH_H

#include <cstddef>
#include <optional>
#include <string>

namespace ned::editor::vcs {

// Extracts a single-hunk patch from raw unified diff output: the enclosing
// file's header block (everything from its "diff --git"/"--- " start up to
// the first "@@") plus the one hunk covering targetLine, both verbatim.
// Returns std::nullopt if no hunk covers targetLine.
//
// targetLine is 1-indexed and counted on the diff's *new* side -- the side
// the buffer's own lines correspond to (worktree content for a plain diff,
// index content for a --cached one). A hunk with newCount > 0 covers
// [newStart, newStart + newCount); a pure-deletion hunk (newCount == 0)
// has no covered line at all -- it sits in the gap after newStart -- and
// matches both boundary lines, newStart and newStart + 1, the latter being
// exactly where BufferView's diff gutter draws its deletion notch
// (0-indexed newStart == 1-indexed newStart + 1; see
// DispatchDiffForTesting), so "point on the marked line" works.
std::optional<std::string> ExtractHunkPatch(const std::string& diffOutput, std::size_t targetLine);

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_DIFFPATCH_H
