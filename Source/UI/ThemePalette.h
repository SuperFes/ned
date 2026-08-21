//
// ThemePalette -- the small semantic palette a full Theme can be derived
// from (rich-theme-set follow-up, Phase 0). Theme itself is ~70 fields;
// hand-authoring that per theme doesn't scale past the two existing
// built-ins and drifts the moment one theme forgets a field. Every theme
// spec people actually publish (base16, Solarized's own table, Catppuccin's,
// Gruvbox's, ...) is a background/foreground pair plus a handful of accent
// hues -- so that's the authoring surface here, and ThemeFromPalette is the
// one place those slots map to Theme's fields. A cloned theme becomes a
// transcription of its official hex values into these slots, nothing more.
//
// The hand-built themes (DarkTheme/LightTheme in Theme.cpp, and the ANSI
// fallback pair, which is deliberately Palette16-only and can't ride a
// TrueColor derivation) stay as they are -- this is additive.
//

#ifndef NED_UI_THEMEPALETTE_H
#define NED_UI_THEMEPALETTE_H

#include <string>

#include "UI/Theme.h"

namespace ned::ui {

struct ThemePalette {
    Color background;
    Color foreground;
    // Everything that should read as receded relative to plain text:
    // comments, punctuation, line numbers, ghost text, markup markers,
    // diagnostic hints, the scroll bar.
    Color subtleForeground;

    // The eight accent hues. Slots are named for the hue, not the role --
    // the role mapping (string=green, keyword=blue, ...) lives in
    // ThemeFromPalette, once, so every derived theme assigns roles
    // identically and only the hues differ. A theme without a real
    // distinct hue for a slot just reuses its nearest neighbor (e.g.
    // orange = yellow), the same way its upstream ports do.
    Color red;
    Color orange;
    Color yellow;
    Color green;
    Color cyan;
    Color blue;
    Color purple;
    Color magenta;

    // UI chrome. chromeBackgroundEmphasis is the "raised" pole (active tab,
    // mode-line gradient start), chromeBackground the quiet one (tab-bar
    // row fill, gradient end) -- mirroring DarkTheme's own
    // 0x2b2b40/0x1b1b30 pair.
    Color chromeBackground;
    Color chromeBackgroundEmphasis;
    Color chromeForeground;
    Color border;
    // The single attention hue of the chrome family: borderAccent, the
    // truncation indicator, and the focused mode-line gradient's tint (the
    // 60% pull DarkTheme's own comment documents) all derive from it.
    Color accent;
    Color selectionBackground;
    Color searchMatchBackground;
};

// Derives a complete Theme. Pure; the returned value is independent of the
// palette afterward. Derived in-between shades (dim tab-bar text, the
// disabled scroll bar, the execution-line wash, the focused gradient) come
// from Color::Interpolate rather than being per-theme literals -- see each
// derivation's own comment in the .cpp.
[[nodiscard]] Theme ThemeFromPalette(std::string name, const ThemePalette& palette);

} // namespace ned::ui

#endif // NED_UI_THEMEPALETTE_H
