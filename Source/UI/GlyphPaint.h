//
// Painting an editor::Glyph's own colour: kept readable against whatever it
// lands on, since brand colours are picked for no theme in particular.
//

#ifndef NED_UI_GLYPHPAINT_H
#define NED_UI_GLYPHPAINT_H

#include <optional>

#include "Compositing.h"
#include "Editor/Glyph.h"
#include "Widget.h"

namespace ned::ui {

// WCAG's floor for non-text graphics.
inline constexpr double kGlyphContrast = 3.0;

// nullopt keeps the surrounding text's colour.
[[nodiscard]] inline std::optional<Color> GlyphForeground(const editor::Glyph& glyph, const Color& background) {
    if (!glyph.rgb) {
        return std::nullopt;
    }
    return EnsureContrast(Color::RGB(*glyph.rgb), background, kGlyphContrast);
}

// Recolours the already-painted cell at `at` that holds glyph.
inline void TintGlyphCell(Canvas& c, Point at, const editor::Glyph& glyph) {
    if (at.x < 0 || at.y < 0 || at.x >= c.size().width || at.y >= c.size().height) {
        return;
    }
    Cell& cell = c[at];
    if (const std::optional<Color> colour = GlyphForeground(glyph, cell.background_color)) {
        cell.foreground_color = *colour;
    }
}

} // namespace ned::ui

#endif // NED_UI_GLYPHPAINT_H
