//
// Lays the composer's text out as rows: split at each newline, then
// word-wrapped to the panel's width. Rows carry the byte range they cover,
// which is what maps the caret, vertical motion and diagnostics between
// the text and the screen.
//

#ifndef NED_UI_ACPPANEL_COMPOSERLAYOUT_H
#define NED_UI_ACPPANEL_COMPOSERLAYOUT_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ned::ui::acppanel {

struct ComposerRow {
    std::string text;
    std::size_t byteStart = 0; // into the laid-out text
    std::size_t byteEnd   = 0; // exclusive; a newline ending the row is outside it
};

struct ComposerLayout {
    std::vector<ComposerRow> rows; // never empty
    int                      caretRow    = 0;
    int                      caretColumn = 0; // display columns into caretRow
};

[[nodiscard]] ComposerLayout LayoutComposer(std::string_view text, std::size_t cursorByte, int width);

// The byte offset on `row` at display column `column`, clamped to the
// row's end -- where a caret moving vertically onto it lands.
[[nodiscard]] std::size_t ByteAtColumn(std::string_view text, const ComposerRow& row, int column);

} // namespace ned::ui::acppanel

#endif // NED_UI_ACPPANEL_COMPOSERLAYOUT_H
