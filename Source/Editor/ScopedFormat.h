//
// configurable-formatter follow-up: automatic scoped on-save -- the Native
// chain (Indent, then Space/Break/Wrap/Blank when configured, then a
// narrower Hygiene rule set) restricted to Buffer::UnsavedChangeRanges()
// snapped to whole lines, so a file converges gradually as it's touched
// rather than exploding into a whole-file diff the first time a barely-
// touched file is saved. format-buffer's own chain (Commands.cpp) stays
// whole-buffer, deliberate -- this is the save-triggered, scoped sibling,
// gated behind AutoFormatOnSave.h and run from saveBufferBody only when
// neither an external FormatCommand() nor a running LSP server already
// claimed the save (both keep their existing, unconditional-whole-buffer
// precedence).
//
// Every rule kind here declines rather than approximates a construct that
// straddles a scope boundary: a Space/Break/Wrap/Blank edit only applies
// when FULLY CONTAINED in the (post-Indent) scope byte range, and a blank-
// line-run collapse is left to format-buffer's own whole-buffer Hygiene
// pass entirely (not attempted scoped at all -- a run beginning above the
// scope and continuing into it has no honest partial answer). Only trim-
// trailing-whitespace-per-line and a final newline (when the scope's last
// line is genuinely the buffer's last line) are scoped here.
//

#ifndef NED_EDITOR_SCOPEDFORMAT_H
#define NED_EDITOR_SCOPEDFORMAT_H

#include "Mode.h"
#include "Text/Buffer.h"

namespace ned::editor {

// Runs the scoped chain over every line range Buffer::UnsavedChangeRanges()
// touches (merged after snapping to whole lines), as one undo step. A no-op
// -- touching neither the buffer nor its undo tree -- for a huge buffer, a
// buffer with no unsaved changes, or when nothing in the configured rules
// actually changes anything. Returns whether anything changed.
bool ApplyScopedFormatOnSave(text::Buffer& buffer, const Mode& mode);

} // namespace ned::editor

#endif // NED_EDITOR_SCOPEDFORMAT_H
