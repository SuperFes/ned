//
// Shared border-drawing primitives (chrome-redesign follow-up) -- the one
// place the app's box-drawing glyph set is defined, so every framed widget
// (ProjectSidebar's own border, TabBar's underline row, WindowManager's
// split divider) draws from the same family instead of each hardcoding its
// own line characters. Rounded corners are the app-wide standard; the glyph
// set is still a parameter so a future theme/config choice can swap it
// without touching any call site's drawing logic.
//

#ifndef NED_UI_BORDER_H
#define NED_UI_BORDER_H

#include <string>
#include <string_view>

#include "Paint.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

struct BorderGlyphs {
    char32_t topLeft;
    char32_t topRight;
    char32_t bottomLeft;
    char32_t bottomRight;
    char32_t horizontal;
    char32_t vertical;
};

// The app-wide standard: light lines with rounded corners.
[[nodiscard]] const BorderGlyphs& RoundedBorderGlyphs();

// Draws a full border rectangle around the outermost cells of the canvas.
// Degenerate sizes degrade sanely: a single row/column becomes a plain
// horizontal/vertical line (corner glyphs only appear when both dimensions
// are >= 2), and a zero-area canvas is a no-op.
void DrawBorder(Canvas& c, const Brush& brush, const BorderGlyphs& glyphs = RoundedBorderGlyphs());

// Embeds `─ Title ─`-style text into an already-drawn top border edge,
// starting at column 2, truncated so the top-right corner cell is never
// overwritten. Codepoint-safe (chrome-widget-utf8 follow-up) via
// PaintUtf8Row below; a canvas too narrow for any text is a no-op.
void DrawBorderTitle(Canvas& c, const std::string& title, const Brush& titleBrush);

// Translucency phase 7b: recolours an already-drawn frame's glyphs from a
// Paint, so a border can be a gradient rather than one flat colour.
//
// Only the frame cells -- the outermost ring of the canvas -- and only their
// *foreground*: a border is a line, and its colour is the line's colour. The
// background those cells already carry is left alone, which is what keeps a
// bordered widget's own fill running underneath the frame.
//
// Each cell samples the paint at its own position within the whole canvas, so
// the axis keywords mean what they look like: `x` runs left to right along
// the top and bottom edges (and is constant down the sides), `y` runs top to
// bottom down the sides, `diag` corner to corner, `radial` from the middle
// out. A paint contributing no colour of its own (an empty one, or a Fade)
// is a no-op, which is what makes the derived solid default byte-identical
// to what DrawBorder already painted.
//
// Call between DrawBorder and DrawBorderTitle: the title is content rather
// than frame, so it keeps its own accent instead of being swept through by
// the gradient.
void RecolourBorder(Canvas& c, const Paint& paint);

// chrome-widget-utf8 follow-up: paints text left-to-right starting at
// (x, y), one whole UTF-8 codepoint per Cell (via Text/Utf8.h's
// NextCodepointBoundary), stopping after maxColumns cells or when text is
// exhausted, whichever comes first. Returns the number of columns actually
// painted -- callers that need to know where the text ended (e.g. placing a
// caret right after typed text) use this instead of text.size(), which
// counts bytes, not columns, once text contains anything outside ASCII.
// Shared by DrawBorderTitle and any widget's own content/input row painting
// (AcpPanel, DebugConsolePanel) that used to index by raw byte instead.
// Not grapheme-cluster- or East-Asian-width-aware -- one codepoint per
// column, same simplification TabBar.cpp's own label painting already made;
// a wider fix is a separate, bigger scope than the mojibake/corruption bug
// this exists to close.
int PaintUtf8Row(Canvas& c, int x, int y, std::string_view text, const Brush& brush, int maxColumns);

} // namespace ned::ui

#endif // NED_UI_BORDER_H
