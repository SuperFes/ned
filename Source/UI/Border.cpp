#include "Border.h"

#include "Text/Utf8.h"

namespace ned::ui {

auto RoundedBorderGlyphs() -> const BorderGlyphs& {
    static const BorderGlyphs glyphs{
        .topLeft     = U'╭',
        .topRight    = U'╮',
        .bottomLeft  = U'╰',
        .bottomRight = U'╯',
        .horizontal  = U'─',
        .vertical    = U'│',
    };
    return glyphs;
}

void DrawBorder(Canvas& c, const Brush& brush, const BorderGlyphs& glyphs) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    const std::string horizontal = text::EncodeCodepointUtf8(glyphs.horizontal);
    const std::string vertical   = text::EncodeCodepointUtf8(glyphs.vertical);

    const auto put = [&c, &brush](int x, int y, const std::string& encoded) {
        Cell& cell     = c[{.x = x, .y = y}];
        cell.character = encoded;
        brush.ApplyTo(cell);
    };

    // Degenerate strips have no room for corners -- a plain line reads
    // better than corner glyphs jammed against each other.
    if (height == 1 && width == 1) {
        put(0, 0, vertical);
        return;
    }

    if (height == 1) {
        for (int x = 0; x < width; ++x) {
            put(x, 0, horizontal);
        }
        return;
    }

    if (width == 1) {
        for (int y = 0; y < height; ++y) {
            put(0, y, vertical);
        }
        return;
    }

    for (int x = 1; x < width - 1; ++x) {
        put(x, 0, horizontal);
        put(x, height - 1, horizontal);
    }

    for (int y = 1; y < height - 1; ++y) {
        put(0, y, vertical);
        put(width - 1, y, vertical);
    }

    put(0, 0, text::EncodeCodepointUtf8(glyphs.topLeft));
    put(width - 1, 0, text::EncodeCodepointUtf8(glyphs.topRight));
    put(0, height - 1, text::EncodeCodepointUtf8(glyphs.bottomLeft));
    put(width - 1, height - 1, text::EncodeCodepointUtf8(glyphs.bottomRight));
}

void DrawBorderTitle(Canvas& c, const std::string& title, const Brush& titleBrush) {
    const int width = c.size().width;
    // Column layout: corner, one line glyph, then the space-padded title,
    // never touching the last two columns (one trailing line glyph plus the
    // top-right corner keeps the frame readable on both sides of the text).
    const int maxTextColumns = width - 4;
    if (maxTextColumns <= 0) {
        return;
    }
    const std::string padded = " " + title + " ";
    PaintUtf8Row(c, 2, 0, padded, titleBrush, maxTextColumns);
}

int PaintUtf8Row(Canvas& c, int x, int y, std::string_view text, const Brush& brush, int maxColumns) {
    int         column = 0;
    std::size_t pos    = 0;
    while (pos < text.size() && column < maxColumns) {
        const std::size_t next = text::NextCodepointBoundary(text, pos);
        Cell&              cell = c[{.x = x + column, .y = y}];
        cell.character          = std::string(text.substr(pos, next - pos));
        brush.ApplyTo(cell);
        pos = next;
        ++column;
    }
    return column;
}

} // namespace ned::ui
