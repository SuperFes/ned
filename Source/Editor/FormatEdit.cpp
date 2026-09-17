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

bool IsFormatWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::size_t LineStartOf(std::string_view text, std::size_t at) {
    if (at == 0) {
        return 0;
    }
    const std::size_t found = text.rfind('\n', at - 1);
    return found == std::string_view::npos ? std::size_t{0} : found + 1;
}

std::string_view LineIndentOf(std::string_view text, std::size_t at) {
    const std::size_t lineStart = LineStartOf(text, at);
    std::size_t       indentEnd = lineStart;
    while (indentEnd < text.size() && (text[indentEnd] == ' ' || text[indentEnd] == '\t')) {
        ++indentEnd;
    }
    return text.substr(lineStart, indentEnd - lineStart);
}

} // namespace ned::editor
