//
// external-modification-safety follow-up: default-on auto-revert of open,
// *unmodified* file-backed buffers whose files changed on disk underneath
// the editor -- the "the file changed and I hadn't touched it, just show me
// the new content" half of external-change handling. The conflicting half
// (the buffer has local edits too) is deliberately not handled here at all:
// a buffer with local edits is never touched, and the save-time
// supersession check (save-buffer, Commands.cpp) owns that conflict via an
// explicit overwrite confirmation instead. Automatic three-way merging of
// both-sides-changed content was considered and deferred -- see ROADMAP.md.
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
