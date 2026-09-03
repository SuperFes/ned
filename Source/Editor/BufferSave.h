//
// buffer-list-panel-save follow-up: the disk-write half of save-buffer --
// backup-before-save, Buffer::Save itself (final-newline/trim-trailing-
// whitespace/line-ending policy, each gated off for a buffer with binary
// safeguards active), and clearing any crash-recovery autosave. Factored
// out of Commands.cpp's save-buffer body so save-some-buffers,
// BufferView::RequestLspFormatThenSaveBuffer (which can't reuse the
// save-buffer command directly -- it needs an LSP round trip first), and
// BufferListPanel's mark-for-save batch execute (no CommandContext at all)
// don't each hand-roll their own copy of the same three-step sequence --
// RequestLspFormatThenSaveBuffer's own prior copy is exactly what motivated
// this extraction. Format-on-save stays call-site-specific (synchronous in
// one caller, LSP-async in another, irrelevant in the rest) and isn't
// folded in here.
//

#ifndef NED_EDITOR_BUFFERSAVE_H
#define NED_EDITOR_BUFFERSAVE_H

#include "Text/Buffer.h"

namespace ned::editor {

// Requires buffer.Path() -- throws std::runtime_error otherwise, same as
// Buffer::Save's own contract. Callers are expected to catch and report,
// matching every existing save-buffer call site.
void WriteBufferToDisk(text::Buffer& buffer);

} // namespace ned::editor

#endif // NED_EDITOR_BUFFERSAVE_H
