#include "ComposerLayout.h"

#include <algorithm>

#include "Text/DisplayWidth.h"
#include "Text/Utf8.h"
#include "TranscriptFormat.h"

namespace ned::ui::acppanel {

ComposerLayout LayoutComposer(std::string_view text, std::size_t cursorByte, int width) {
    ComposerLayout layout;
    cursorByte             = std::min(cursorByte, text.size());
    std::size_t lineStart  = 0;
    bool        caretFound = false;
    while (true) {
        const std::size_t newline = text.find('\n', lineStart);
        const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;
        // WordWrap's rows concatenate back to the line exactly, so their
        // byte ranges follow from their lengths.
        std::size_t rowStart = lineStart;
        for (WrappedRow& wrapped : WordWrap(text.substr(lineStart, lineEnd - lineStart), width)) {
            const std::size_t rowEnd = rowStart + wrapped.text.size();
            if (!caretFound && cursorByte >= rowStart && cursorByte <= rowEnd) {
                layout.caretRow    = static_cast<int>(layout.rows.size());
                layout.caretColumn = text::StringColumns(text.substr(rowStart, cursorByte - rowStart));
                caretFound         = true;
            }
            layout.rows.push_back({.text = std::move(wrapped.text), .byteStart = rowStart, .byteEnd = rowEnd});
            rowStart = rowEnd;
        }
        if (newline == std::string_view::npos) {
            break;
        }
        lineStart = newline + 1;
    }
    return layout;
}

std::size_t ByteAtColumn(std::string_view text, const ComposerRow& row, int column) {
    std::size_t pos     = row.byteStart;
    int         columns = 0;
    while (pos < row.byteEnd) {
        const text::Glyph glyph = text::GlyphAt(text, pos);
        if (columns + glyph.columns > column) {
            break;
        }
        columns += glyph.columns;
        pos += glyph.byteLength;
    }
    return pos;
}

} // namespace ned::ui::acppanel
