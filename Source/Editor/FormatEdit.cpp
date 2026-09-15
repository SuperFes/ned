#include "FormatEdit.h"

#include <algorithm>

namespace ned::editor {

void ApplyFormatTextEdits(text::Buffer& buffer, std::vector<FormatTextEdit> edits) {
    if (edits.empty()) {
        return;
    }
    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });

    buffer.BeginUndoGroup();
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        buffer.DeleteRange(it->start, it->end - it->start); // DeleteRange's 2nd argument is a LENGTH, not an end offset
        buffer.InsertAt(it->start, it->text);
    }
    buffer.EndUndoGroup();
}

bool IsWordByte(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

} // namespace ned::editor
