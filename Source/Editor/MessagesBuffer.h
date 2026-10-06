//
// The "*Messages*" buffer: DiagnosticsLog's in-memory ring rendered into a
// read-only text buffer. Kept apart from DiagnosticsLog so logging itself
// needs no text layer.
//

#ifndef NED_EDITOR_MESSAGESBUFFER_H
#define NED_EDITOR_MESSAGESBUFFER_H

#include <string_view>

#include "Text/BufferList.h"

namespace ned::editor {

// Finds-or-creates "*Messages*" and replaces its content wholesale with one
// line per entry whose category is currently visible (oldest first),
// formatted "HH:MM:SS [SEVERITY] [category] message (path:line)". Attaches
// one synthetic Buffer::Diagnostic per line (ranged over that line's own
// bytes, severity mapped from LogSeverity) so the existing diagnostics
// gutter glyph/color pipeline renders severity for free -- see
// BufferView.cpp's DiagnosticGlyphFor. Ordinary DeleteRange/InsertAtPoint
// wrapped in one BeginUndoGroup/EndUndoGroup step, not
// Buffer::ReplaceContentForLoad (that API is tied to the async-file-loader's
// IsLoading() state machine specifically).
void RebuildMessagesBuffer(text::BufferList& bufferList);

[[nodiscard]] std::string_view MessagesBufferName(); // "*Messages*"

} // namespace ned::editor

#endif // NED_EDITOR_MESSAGESBUFFER_H
