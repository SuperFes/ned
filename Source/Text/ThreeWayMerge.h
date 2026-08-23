//
// external-modification-round-2 follow-up: a pure, buffer-free line-based
// three-way (diff3-style) merge -- no dependency on Buffer or any other
// part of this codebase, the same "composable primitive" shape KillRing.h
// establishes. The caller (Buffer::MergeExternalChanges) supplies base
// (the common ancestor -- Buffer::SavedSnapshot_, the content as of last
// load/save), ours (the buffer's own local edits), and theirs (fresh disk
// content) as whole-file text.
//
// Line-granular, matching git merge-file's own default: splits on '\n',
// each line token keeping its own terminator so reassembly is plain
// concatenation. A hunk region touched by only one side is taken
// automatically; touched by both sides with the *same* resulting content
// is also taken automatically (not a real conflict); only a region where
// both sides genuinely diverge becomes a conflict, marked with the
// standard git conflict-marker convention rather than any bespoke format,
// so no new interactive resolution UI is needed -- a user already knows
// how to hand-edit these.
//

#ifndef NED_TEXT_THREEWAYMERGE_H
#define NED_TEXT_THREEWAYMERGE_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ned::text {

struct MergeResult {
    std::string mergedText;
    std::size_t conflictCount = 0;
    // Byte offset of the first "<<<<<<<" marker in mergedText, if any.
    std::optional<std::size_t> firstConflictOffset;
};

[[nodiscard]] MergeResult ThreeWayMerge(std::string_view base, std::string_view ours, std::string_view theirs);

// True if text contains a "<<<<<<< " conflict-start marker at the
// beginning of a line -- shared by save-buffer's own guard against
// silently writing unresolved markers to disk.
[[nodiscard]] bool HasConflictMarkers(std::string_view text);

} // namespace ned::text

#endif // NED_TEXT_THREEWAYMERGE_H
