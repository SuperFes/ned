//
// The pure half of ned's own alpha compositing -- no Screen, no Notcurses,
// no widget state, so every rule here is unit-testable in isolation
// (Tests/CompositingTest.cpp).
//
// Why ned composites at all, rather than asking Notcurses to: every widget
// paints into one shared Screen of Cells which is flushed to the terminal
// once per frame, so ned already owns a compositor and can do exact 8-bit
// alpha. Notcurses' own cell alpha is a 2-bit enum whose BLEND mode is a
// fixed 50/50 with whatever is beneath -- measured, along with everything
// else this file assumes, by Tools/NotcursesGradientProbe.cpp. See
// Docs/Translucency.md for the whole design.
//
// The constraint that shapes all of it: a cell is one glyph plus one
// foreground plus one background. There is no layering *within* a cell, so
// a background wash and a see-through background are mutually exclusive,
// and a cell already carrying a code glyph cannot also carry a dither
// pattern. Hence three separate techniques rather than one blend function.
//

#ifndef NED_UI_COMPOSITING_H
#define NED_UI_COMPOSITING_H

#include <string>
#include <string_view>

#include "Widget.h"

namespace ned::ui {

// T1: `src` over `dst`, using src's alpha. The result is always opaque --
// compositing resolves alpha away rather than accumulating it, since the
// destination is a real screen cell and not a layer. A non-composable
// destination (Default, or a palette index) has no RGB to blend with, so
// the caller must have picked a different technique; this returns `src`
// made opaque rather than inventing a backdrop.
[[nodiscard]] Color BlendOver(const Color& dst, const Color& src);

// T3: pull `dst` toward `wash` by the wash's own alpha, leaving alpha
// opaque. Used for foreground tinting (a highlight that keeps the syntax
// color and the transparent background) and for Fade paints. A Default
// destination is returned unchanged for a *translucent* wash -- the
// terminal's own foreground has no RGB value we could move -- but an opaque
// wash replaces it outright, since "nothing of the original survives" needs
// no knowledge of the original.
[[nodiscard]] Color TintToward(const Color& dst, const Color& wash);

// T2: the glyph that represents `coverage` (0..1) at this cell, using an
// 8x8 ordered-dither matrix indexed by *absolute* cell position -- which is
// what keeps a coverage ramp smooth across cell boundaries instead of
// striping, since a per-cell pattern makes every cell in a band identical.
//
// Returns "" for nothing to draw, U+2588 for full coverage (braille dots
// never fully cover a cell -- even U+28FF leaves the inter-dot gaps
// showing), and a braille cell for everything between.
[[nodiscard]] std::string DitherGlyph(double coverage, int x, int y);

// WCAG relative luminance and contrast ratio (1.0 for identical colours,
// 21.0 for black against white). Uncomposable colours have no luminance we
// could compute, so a pair involving one reports 21.0 -- "assume it is
// fine" rather than "assume it is broken", since the terminal's own
// foreground/background pair is the user's own choice and not ours to
// second-guess.
[[nodiscard]] double RelativeLuminance(const Color& colour);
[[nodiscard]] double ContrastRatio(const Color& a, const Color& b);

// The contrast guard: returns `foreground` moved toward black or white --
// whichever direction the background is further from -- until it clears
// `minRatio` against `background`, or unchanged if it already does (or if
// either colour has no RGB to work with).
//
// This exists because translucency costs contrast by construction: every
// wash over code pulls the text toward the wash's own colour, and with the
// ANSI fallback path gone a theme has nothing else to hide behind. See
// Docs/Translucency.md.
[[nodiscard]] Color EnsureContrast(const Color& foreground, const Color& background, double minRatio);

// Whether a cell is carrying real content, i.e. whether a pattern or dither
// would be destroying something. A space is not content.
[[nodiscard]] bool IsBlankGlyph(std::string_view character);

} // namespace ned::ui

#endif // NED_UI_COMPOSITING_H
