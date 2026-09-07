//
// Merge Conflict Resolution Mode: the thin Buffer-mutating layer on top of
// Text/ConflictHunk.h's pure parse -- Org.h/OrgCapture.h's own "pure parse
// plus mutating layer" split. Deliberately not a mode/session object: each
// resolution is one ordinary, independent Buffer edit, so there is nothing
// to get stuck in -- manual editing works before, during, and after any of
// these calls exactly like it always has.
//

#ifndef NED_EDITOR_CONFLICTRESOLUTION_H
#define NED_EDITOR_CONFLICTRESOLUTION_H

#include <cstddef>
#include <optional>

#include "Text/Buffer.h"
#include "Text/ConflictHunk.h"

namespace ned::editor {

enum class ConflictResolution {
    TakeOurs,
    TakeTheirs,
    TakeBoth,   // ours then theirs
    TakeNeither, // delete the whole marked block
    KeepBase,   // diff3 only -- caller must check hunk.baseRange first
};

// The conflict hunk containing `point`, if any -- no "nearest hunk"
// fallback, matching StageOrUnstageHunkAtPoint's own precedent that point
// must genuinely be inside the thing being acted on.
[[nodiscard]] std::optional<text::ConflictHunk> ConflictHunkAtPoint(const text::Buffer& buffer, std::size_t point);

// Replaces hunk.startByte..endByte with the resolved content as one undo
// step, point left at the start of the replacement. Returns false with no
// buffer change for ConflictResolution::KeepBase on a hunk with no
// baseRange -- the only caller-checkable no-op case.
bool ResolveConflictHunk(text::Buffer& buffer, const text::ConflictHunk& hunk, ConflictResolution resolution);

// Point motion only, wrapping around the buffer -- re-parses fresh each call
// (a cheap O(n) scan; resolving a hunk changes the parse immediately after,
// so there's no cache worth maintaining at this layer).
[[nodiscard]] std::optional<std::size_t> NextConflictHunkStart(const text::Buffer& buffer, std::size_t point);
[[nodiscard]] std::optional<std::size_t> PreviousConflictHunkStart(const text::Buffer& buffer, std::size_t point);

} // namespace ned::editor

#endif // NED_EDITOR_CONFLICTRESOLUTION_H
