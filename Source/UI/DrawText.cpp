#include "DrawText.h"

namespace ned::ui {

int DrawGlyph(Canvas& c, int x, int y, const std::string& text, int columns, const Brush& brush, bool textOnly) {
    const auto apply = [&](Cell& cell) {
        if (textOnly) {
            brush.ApplyTextTo(cell);
        }
        else {
            brush.ApplyTo(cell);
        }
    };
    Cell& cell = c[{.x = x, .y = y}];
    apply(cell);
    if (columns == 2 && x + 1 < c.size().width) {
        cell.character = text;
        Cell& right    = c[{.x = x + 1, .y = y}];
        apply(right);
        right.character = "";
    }
    else {
        cell.character = columns == 2 ? " " : text;
    }
    return columns;
}

int DrawText(Canvas& c, int x, int y, std::string_view text, const Brush& brush, int maxX, const TextOptions& options) {
    int col = x;
    for (std::size_t offset = 0; offset < text.size();) {
        const text::Glyph glyph   = text::GlyphAt(text, offset);
        const int         columns = text::GlyphColumns(glyph, options.startColumn + (col - x), options.tabWidth);
        if (col + columns > maxX) {
            break;
        }
        if (glyph.codepoint == U'\t' || glyph.placeholder) {
            // Each cell of an expanded tab or a placeholder is narrow.
            const std::string cells = glyph.placeholder ? text::PlaceholderText(glyph) : std::string(columns, ' ');
            for (std::size_t at = 0; at < cells.size();) {
                const text::Glyph part = text::GlyphAt(cells, at);
                col += DrawGlyph(c, col, y, cells.substr(at, part.byteLength), 1, brush, options.textOnly);
                at += part.byteLength;
            }
        }
        else {
            col += DrawGlyph(c, col, y, text::GlyphText(text, offset, glyph), glyph.columns, brush, options.textOnly);
        }
        offset += glyph.byteLength;
    }
    return col - x;
}

std::vector<std::string> TextCells(std::string_view text) {
    std::vector<std::string> cells;
    for (std::size_t offset = 0; offset < text.size();) {
        const text::Glyph glyph = text::GlyphAt(text, offset);
        if (glyph.placeholder || glyph.codepoint == U'\t') {
            const std::string expanded = glyph.placeholder ? text::PlaceholderText(glyph) : std::string(" ");
            for (std::size_t at = 0; at < expanded.size();) {
                const std::size_t length = text::GlyphAt(expanded, at).byteLength;
                cells.push_back(expanded.substr(at, length));
                at += length;
            }
        }
        else {
            cells.push_back(text::GlyphText(text, offset, glyph));
            if (glyph.columns == 2) {
                cells.emplace_back();
            }
        }
        offset += glyph.byteLength;
    }
    return cells;
}

} // namespace ned::ui
