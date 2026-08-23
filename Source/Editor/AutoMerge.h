//
// external-modification-round-2 follow-up: AutoRevert.h's own doc comment
// calls this out as deferred -- the conflicting half it deliberately skips
// (a buffer with local edits AND a file that also changed on disk)
// handled here instead, via Buffer::MergeExternalChanges (a three-way
// merge, Text/ThreeWayMerge.h) rather than AutoRevert's own discard-and-
// reload. Same default-on, mutex-guarded-static-toggle shape as
// AutoRevert.h, but a separate toggle from ned/set-auto-revert: this
// touches a buffer's own local edits, a materially bigger intervention
// than reverting an already-unmodified one, even though it's
// non-destructive (one undo step, never discards -- a clean merge just
// combines, a conflicted one marks rather than guesses).
//

#ifndef NED_EDITOR_AUTOMERGE_H
#define NED_EDITOR_AUTOMERGE_H

#include <cstddef>
#include <string>
#include <vector>

#include "Text/BufferList.h"

namespace ned::editor {

void               SetAutoMergeEnabled(bool enabled);
[[nodiscard]] bool AutoMergeEnabled();

struct AutoMergeResult {
    std::string name;
    std::size_t conflictCount;
};

// Runs Buffer::MergeExternalChanges() on every open, *modified*,
// externally-changed, file-backed buffer -- the inverse gating of
// AutoRevertBuffers (which only ever acts on *unmodified* ones), so the
// two sweeps never touch the same buffer on the same tick. Returns one
// entry per merged buffer (conflictCount == 0 for a fully automatic,
// silent merge) so the caller can surface a status message. A no-op
// returning empty when the toggle is off. Per-buffer failures are
// swallowed rather than propagated, matching AutoRevertBuffers' own
// unattended-timer-context posture.
[[nodiscard]] std::vector<AutoMergeResult> AutoMergeBuffers(text::BufferList& bufferList);

} // namespace ned::editor

#endif // NED_EDITOR_AUTOMERGE_H
