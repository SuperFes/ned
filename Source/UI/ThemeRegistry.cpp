#include "ThemeRegistry.h"

#include <algorithm>

#include "UI/ThemePalette.h"

namespace ned::ui {

namespace {

    // The eight original palette-derived themes (rich-theme-set follow-up,
    // Phase 2), reachable by name only -- nothing outside this table needs
    // a direct factory, and tests go through ThemeByName like everything
    // else. The taxonomy is the user's own: "major" = vivid saturated
    // primaries, "minor" = muted/pastel, plus a high-contrast pair (pure
    // black/white backgrounds, held to a raised contrast floor by
    // BundledThemesTest) and a monochrome pair. Monochrome needs no special
    // derivation variant, despite the plan's initial guess (ROADMAP):
    // filling the eight accent *hue* slots with grayscale luminance shades
    // sends a single-ramp palette through the standard ThemeFromPalette
    // role mapping unchanged -- BrushFor's bold/italic traits carry the
    // semantics hue can't, and BundledThemesTest's grayscale invariant
    // (every serialized color has r==g==b) keeps it honestly monochrome.

    Theme MajorDarkTheme() {
        return ThemeFromPalette("major-dark",
                                ThemePalette{
                                    .background               = Color::RGB(0x14141c),
                                    .foreground               = Color::RGB(0xe8e8e8),
                                    .subtleForeground         = Color::RGB(0x7a8296),
                                    .red                      = Color::RGB(0xff4d4d),
                                    .orange                   = Color::RGB(0xff9933),
                                    .yellow                   = Color::RGB(0xffd633),
                                    .green                    = Color::RGB(0x33cc66),
                                    .cyan                     = Color::RGB(0x2bd6d6),
                                    .blue                     = Color::RGB(0x4d94ff),
                                    .purple                   = Color::RGB(0xb366ff),
                                    .magenta                  = Color::RGB(0xff4dd2),
                                    .chromeBackground         = Color::RGB(0x1c1c28),
                                    .chromeBackgroundEmphasis = Color::RGB(0x28283a),
                                    .chromeForeground         = Color::RGB(0xf0f0f5),
                                    .border                   = Color::RGB(0x3c3c55),
                                    .accent                   = Color::RGB(0x8f80ff),
                                    .selectionBackground      = Color::RGB(0x2e4a80),
                                    .searchMatchBackground    = Color::RGB(0x806a1a),
                                });
    }

    Theme MajorLightTheme() {
        return ThemeFromPalette("major-light",
                                ThemePalette{
                                    .background               = Color::RGB(0xfcfcfa),
                                    .foreground               = Color::RGB(0x1c1c24),
                                    .subtleForeground         = Color::RGB(0x70707c),
                                    .red                      = Color::RGB(0xd42030),
                                    .orange                   = Color::RGB(0xc26010),
                                    .yellow                   = Color::RGB(0xa8830a),
                                    .green                    = Color::RGB(0x1a9e37),
                                    .cyan                     = Color::RGB(0x0a8fa8),
                                    .blue                     = Color::RGB(0x1f5fe0),
                                    .purple                   = Color::RGB(0x8b2fc9),
                                    .magenta                  = Color::RGB(0xc4189c),
                                    .chromeBackground         = Color::RGB(0xefefec),
                                    .chromeBackgroundEmphasis = Color::RGB(0xe2e2de),
                                    .chromeForeground         = Color::RGB(0x24242c),
                                    .border                   = Color::RGB(0xc6c6c0),
                                    .accent                   = Color::RGB(0x5a48d6),
                                    .selectionBackground      = Color::RGB(0xcfe0f7),
                                    .searchMatchBackground    = Color::RGB(0xf7e08a),
                                });
    }

    Theme MinorDarkTheme() {
        return ThemeFromPalette("minor-dark",
                                ThemePalette{
                                    .background               = Color::RGB(0x232529),
                                    .foreground               = Color::RGB(0xc5c8ce),
                                    .subtleForeground         = Color::RGB(0x7d828c),
                                    .red                      = Color::RGB(0xd48a8a),
                                    .orange                   = Color::RGB(0xd4a878),
                                    .yellow                   = Color::RGB(0xd6c58f),
                                    .green                    = Color::RGB(0xa3c293),
                                    .cyan                     = Color::RGB(0x8fbcbb),
                                    .blue                     = Color::RGB(0x92aecc),
                                    .purple                   = Color::RGB(0xb3a0cc),
                                    .magenta                  = Color::RGB(0xc79ec4),
                                    .chromeBackground         = Color::RGB(0x1d1f23),
                                    .chromeBackgroundEmphasis = Color::RGB(0x2b2e34),
                                    .chromeForeground         = Color::RGB(0xd8dade),
                                    .border                   = Color::RGB(0x3a3d45),
                                    .accent                   = Color::RGB(0x9a8fc2),
                                    .selectionBackground      = Color::RGB(0x3a4250),
                                    .searchMatchBackground    = Color::RGB(0x5c5433),
                                });
    }

    Theme MinorLightTheme() {
        return ThemeFromPalette("minor-light",
                                ThemePalette{
                                    .background               = Color::RGB(0xf5f2ec),
                                    .foreground               = Color::RGB(0x4a4a48),
                                    .subtleForeground         = Color::RGB(0x9a968c),
                                    .red                      = Color::RGB(0xa85f5c),
                                    .orange                   = Color::RGB(0xa07648),
                                    .yellow                   = Color::RGB(0x94804a),
                                    .green                    = Color::RGB(0x6e8f5e),
                                    .cyan                     = Color::RGB(0x50888f),
                                    .blue                     = Color::RGB(0x5a7ca6),
                                    .purple                   = Color::RGB(0x84719e),
                                    .magenta                  = Color::RGB(0x9d6b94),
                                    .chromeBackground         = Color::RGB(0xe9e5dc),
                                    .chromeBackgroundEmphasis = Color::RGB(0xdcd7cc),
                                    .chromeForeground         = Color::RGB(0x3c3c3a),
                                    .border                   = Color::RGB(0xc8c2b4),
                                    .accent                   = Color::RGB(0x7a6aa8),
                                    .selectionBackground      = Color::RGB(0xd6dced),
                                    .searchMatchBackground    = Color::RGB(0xecd9a0),
                                });
    }

    Theme HighContrastDarkTheme() {
        return ThemeFromPalette("high-contrast-dark",
                                ThemePalette{
                                    .background               = Color::RGB(0x000000),
                                    .foreground               = Color::RGB(0xffffff),
                                    .subtleForeground         = Color::RGB(0xb0b0b0),
                                    .red                      = Color::RGB(0xff5252),
                                    .orange                   = Color::RGB(0xffa040),
                                    .yellow                   = Color::RGB(0xffe135),
                                    .green                    = Color::RGB(0x4dff6e),
                                    .cyan                     = Color::RGB(0x40e8ff),
                                    .blue                     = Color::RGB(0x66b2ff),
                                    .purple                   = Color::RGB(0xcc8aff),
                                    .magenta                  = Color::RGB(0xff6ef2),
                                    .chromeBackground         = Color::RGB(0x0a0a0a),
                                    .chromeBackgroundEmphasis = Color::RGB(0x1f1f1f),
                                    .chromeForeground         = Color::RGB(0xffffff),
                                    .border                   = Color::RGB(0x9a9a9a),
                                    .accent                   = Color::RGB(0xffe135),
                                    .selectionBackground      = Color::RGB(0x2160c4),
                                    .searchMatchBackground    = Color::RGB(0x705e00),
                                });
    }

    Theme HighContrastLightTheme() {
        return ThemeFromPalette("high-contrast-light",
                                ThemePalette{
                                    .background               = Color::RGB(0xffffff),
                                    .foreground               = Color::RGB(0x000000),
                                    .subtleForeground         = Color::RGB(0x4a4a4a),
                                    .red                      = Color::RGB(0xc00000),
                                    .orange                   = Color::RGB(0x9a4a00),
                                    .yellow                   = Color::RGB(0x806000),
                                    .green                    = Color::RGB(0x006e00),
                                    .cyan                     = Color::RGB(0x006070),
                                    .blue                     = Color::RGB(0x0000d0),
                                    .purple                   = Color::RGB(0x6a00b0),
                                    .magenta                  = Color::RGB(0xa00080),
                                    .chromeBackground         = Color::RGB(0xf2f2f2),
                                    .chromeBackgroundEmphasis = Color::RGB(0xe0e0e0),
                                    .chromeForeground         = Color::RGB(0x000000),
                                    .border                   = Color::RGB(0x666666),
                                    .accent                   = Color::RGB(0x0000d0),
                                    .selectionBackground      = Color::RGB(0xa6c8ff),
                                    .searchMatchBackground    = Color::RGB(0xffe135),
                                });
    }

    // Grayscale ramps: role separation comes from luminance steps (keyword
    // brightest, then operator/function, down through types/numbers) plus
    // BrushFor's own bold/italic traits -- see the block comment above.
    Theme MonoDarkTheme() {
        return ThemeFromPalette("mono-dark",
                                ThemePalette{
                                    .background               = Color::RGB(0x101010),
                                    .foreground               = Color::RGB(0xdadada),
                                    .subtleForeground         = Color::RGB(0x8a8a8a),
                                    .red                      = Color::RGB(0xf5f5f5),
                                    .orange                   = Color::RGB(0xcfcfcf),
                                    .yellow                   = Color::RGB(0xbdbdbd),
                                    .green                    = Color::RGB(0xa8a8a8),
                                    .cyan                     = Color::RGB(0xe3e3e3),
                                    .blue                     = Color::RGB(0xffffff),
                                    .purple                   = Color::RGB(0xb3b3b3),
                                    .magenta                  = Color::RGB(0x9c9c9c),
                                    .chromeBackground         = Color::RGB(0x1a1a1a),
                                    .chromeBackgroundEmphasis = Color::RGB(0x2a2a2a),
                                    .chromeForeground         = Color::RGB(0xececec),
                                    .border                   = Color::RGB(0x484848),
                                    .accent                   = Color::RGB(0xffffff),
                                    .selectionBackground      = Color::RGB(0x3c3c3c),
                                    .searchMatchBackground    = Color::RGB(0x5a5a5a),
                                });
    }

    Theme MonoLightTheme() {
        return ThemeFromPalette("mono-light",
                                ThemePalette{
                                    .background               = Color::RGB(0xfafafa),
                                    .foreground               = Color::RGB(0x202020),
                                    .subtleForeground         = Color::RGB(0x8c8c8c),
                                    .red                      = Color::RGB(0x0a0a0a),
                                    .orange                   = Color::RGB(0x3a3a3a),
                                    .yellow                   = Color::RGB(0x505050),
                                    .green                    = Color::RGB(0x6a6a6a),
                                    .cyan                     = Color::RGB(0x2a2a2a),
                                    .blue                     = Color::RGB(0x000000),
                                    .purple                   = Color::RGB(0x4a4a4a),
                                    .magenta                  = Color::RGB(0x606060),
                                    .chromeBackground         = Color::RGB(0xececec),
                                    .chromeBackgroundEmphasis = Color::RGB(0xdedede),
                                    .chromeForeground         = Color::RGB(0x101010),
                                    .border                   = Color::RGB(0xb4b4b4),
                                    .accent                   = Color::RGB(0x000000),
                                    .selectionBackground      = Color::RGB(0xd2d2d2),
                                    .searchMatchBackground    = Color::RGB(0xbfbfbf),
                                });
    }

    struct ThemeFactory {
        std::string_view name;
        Theme (*make)();
    };

    // The one table (see the header comment). Each entry's name must match
    // the .name the factory itself sets -- checked by ThemeRegistryTest's
    // round-trip, not trusted.
    constexpr ThemeFactory kThemeFactories[] = {
        {"dark", DarkTheme},
        {"light", LightTheme},
        {"ansi-dark", AnsiDarkTheme},
        {"ansi-light", AnsiLightTheme},
        {"major-dark", MajorDarkTheme},
        {"major-light", MajorLightTheme},
        {"minor-dark", MinorDarkTheme},
        {"minor-light", MinorLightTheme},
        {"high-contrast-dark", HighContrastDarkTheme},
        {"high-contrast-light", HighContrastLightTheme},
        {"mono-dark", MonoDarkTheme},
        {"mono-light", MonoLightTheme},
    };

} // namespace

std::optional<Theme> ThemeByName(std::string_view name) {
    for (const ThemeFactory& factory : kThemeFactories) {
        if (factory.name == name) {
            return factory.make();
        }
    }
    return std::nullopt;
}

std::vector<std::string> ThemeNames() {
    std::vector<std::string> names;
    names.reserve(std::size(kThemeFactories));
    for (const ThemeFactory& factory : kThemeFactories) {
        names.emplace_back(factory.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace ned::ui
