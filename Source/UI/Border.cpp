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
    std::string padded = " " + title + " ";
    if (static_cast<int>(padded.size()) > maxTextColumns) {
        padded.resize(static_cast<std::size_t>(maxTextColumns));
    }
    for (int i = 0; i < static_cast<int>(padded.size()); ++i) {
        Cell& cell     = c[{.x = 2 + i, .y = 0}];
        cell.character = std::string(1, padded[static_cast<std::size_t>(i)]);
        titleBrush.ApplyTo(cell);
    }
}

} // namespace ned::ui
