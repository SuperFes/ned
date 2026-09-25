//
// Writing text into a Canvas a glyph at a time, at the width the renderer
// gives it (Text/DisplayWidth.h). Every widget that puts a string on screen
// goes through here, so a wide glyph always carries its continuation cell
// and nothing is ever drawn as half a glyph.
//

#ifndef NED_UI_DRAWTEXT_H
#define NED_UI_DRAWTEXT_H

#include <string>
#include <string_view>
#include <vector>

#include "Text/DisplayWidth.h"
#include "UI/Theme.h"
#include "UI/Widget.h"

namespace ned::ui {

struct TextOptions {
    // A tab expands to spaces up to the next multiple of tabWidth, counted
    // from startColumn at the text's first cell.
    int tabWidth    = 1;
    int startColumn = 0;
    // Brush::ApplyTextTo rather than ApplyTo: glyph, foreground and traits
    // only, so a background painted beforehand survives.
    bool textOnly = false;
};

// Draws one glyph -- `text`, `columns` cells wide -- at (x, y) and returns
// `columns`. A two-column glyph claims the cell to its right as its
// continuation; one with no room for that draws as a blank instead.
int DrawGlyph(Canvas& c, int x, int y, const std::string& text, int columns, const Brush& brush,
              bool textOnly = false);

// Draws text from (x, y) glyph by glyph, stopping before column maxX (never
// half a glyph), and returns the columns drawn. A control or zero-width
// character draws as its placeholder.
int DrawText(Canvas& c, int x, int y, std::string_view text, const Brush& brush, int maxX,
             const TextOptions& options = {});

// text split into the cells it occupies, one entry per cell: a wide glyph's
// right half is "" (its continuation), a control character's placeholder a
// cell per character. For widgets that style or clip cell by cell.
[[nodiscard]] std::vector<std::string> TextCells(std::string_view text);

} // namespace ned::ui

#endif // NED_UI_DRAWTEXT_H
