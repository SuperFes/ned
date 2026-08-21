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
// overwritten. The title is treated as single-column-per-byte ASCII (the
// same assumption ProjectSidebar's own label painting makes); a canvas too
// narrow for any text is a no-op.
void DrawBorderTitle(Canvas& c, const std::string& title, const Brush& titleBrush);

} // namespace ned::ui

#endif // NED_UI_BORDER_H
