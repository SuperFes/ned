//
// external-modification-safety follow-up: default-on auto-revert of open,
// *unmodified* file-backed buffers whose files changed on disk underneath
// the editor -- the "the file changed and I hadn't touched it, just show me
// the new content" half of external-change handling. The conflicting half
// (the buffer has local edits too) is deliberately not handled here at all:
// a buffer with local edits is never touched by this sweep -- see
// AutoMerge.h's AutoMergeBuffers for that half (a three-way merge via
// Buffer::MergeExternalChanges, not a discard). The save-time supersession
// check (save-buffer, Commands.cpp) still separately guards a save against
// overwriting a file this sweep hasn't caught up to yet.
//

#ifndef NED_EDITOR_AUTOREVERT_H
#define NED_EDITOR_AUTOREVERT_H

#include <string>
#include <vector>

#include "Text/BufferList.h"

namespace ned::editor {

// Process-wide toggle (mutex-guarded static state, mirroring TabWidth.h/
// ScratchPad.h's exact pattern), default on. Configured from Janet via
// ned/set-auto-revert.
void               SetAutoRevertEnabled(bool enabled);
[[nodiscard]] bool AutoRevertEnabled();

// Reverts every open, unmodified, file-backed buffer whose file changed on
// disk since it was last loaded/saved (Buffer::ExternallyModified), and
// returns the names of the buffers actually reverted so the caller can
// surface a status message. A no-op returning empty when the toggle is
// off. Per-buffer failures are swallowed rather than propagated -- this
// runs unattended on the same timer tick as AutoSaveScratchBuffers, with
// nothing to report a failure to (matching that function's own posture);
// a buffer whose file was deleted simply never reports ExternallyModified
// and is left alone.
std::vector<std::string> AutoRevertBuffers(text::BufferList& bufferList);

} // namespace ned::editor

#endif // NED_EDITOR_AUTOREVERT_H
