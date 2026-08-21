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

    // Fuchsia (user request, post-Phase-3): a dark theme built *around* one
    // signature hue rather than a balanced spread -- fuchsia holds the
    // keyword slot and the whole chrome accent family (borders' attention
    // pole, truncation indicator, focused mode-line tint), on a dark plum
    // background tinted toward it. The supporting cast is chosen on the
    // color wheel relative to fuchsia: mint green (its complement) for
    // strings, teal (split-complementary) for functions, warm gold/coral
    // (triadic-side warmth) for types and annotations, and the red/magenta/
    // purple slots stay in fuchsia's own analogous raspberry-pink-violet
    // family so nothing clashes with the star.
    Theme FuchsiaTheme() {
        return ThemeFromPalette("fuchsia",
                                ThemePalette{
                                    .background               = Color::RGB(0x1e1526), // dark plum
                                    .foreground               = Color::RGB(0xe8dff0),
                                    .subtleForeground         = Color::RGB(0x8a7a99),
                                    .red                      = Color::RGB(0xff5577), // raspberry
                                    .orange                   = Color::RGB(0xff9070), // coral
                                    .yellow                   = Color::RGB(0xf0c060), // warm gold
                                    .green                    = Color::RGB(0x5fe0a8), // mint -- fuchsia's complement
                                    .cyan                     = Color::RGB(0x52d5e8), // teal -- split-complementary
                                    .blue                     = Color::RGB(0xf042d6), // fuchsia itself: the keyword slot
                                    .purple                   = Color::RGB(0xb48cfa), // violet
                                    .magenta                  = Color::RGB(0xff6ec7), // hot pink
                                    .chromeBackground         = Color::RGB(0x170f1e),
                                    .chromeBackgroundEmphasis = Color::RGB(0x2c2038),
                                    .chromeForeground         = Color::RGB(0xf2e6f7),
                                    .border                   = Color::RGB(0x4a3a5c),
                                    .accent                   = Color::RGB(0xf042d6), // fuchsia again -- one signature, everywhere
                                    .selectionBackground      = Color::RGB(0x4d2b54),
                                    .searchMatchBackground    = Color::RGB(0x6b5a20),
                                });
    }

    // The cloned theme set (rich-theme-set follow-up, Phase 3): transcriptions
    // of the internet's most-shipped palettes into ThemePalette slots, each
    // with its upstream source. Slot assignment is by *hue*, not by upstream
    // role -- a palette without a real hue for a slot reuses its nearest
    // neighbor (ThemePalette.h documents this), so e.g. Dracula's keywords
    // come out purple-bold rather than upstream's pink-italic: the palette is
    // authentic, the role mapping is ned's own, uniformly across all clones.
    // Color palettes are uncopyrightable facts, and every upstream project
    // here ships under MIT (or, for classic Monokai, is universally
    // redistributed); names kept as-is per universal editor practice.

    // Solarized, Ethan Schoonover -- https://github.com/altercation/solarized
    // (MIT). The 16-color table is shared; dark/light swap which base tones
    // serve as background/content, exactly as upstream specifies.
    Theme SolarizedDarkTheme() {
        return ThemeFromPalette("solarized-dark",
                                ThemePalette{
                                    .background               = Color::RGB(0x002b36), // base03
                                    .foreground               = Color::RGB(0x839496), // base0
                                    .subtleForeground         = Color::RGB(0x586e75), // base01
                                    .red                      = Color::RGB(0xdc322f),
                                    .orange                   = Color::RGB(0xcb4b16),
                                    .yellow                   = Color::RGB(0xb58900),
                                    .green                    = Color::RGB(0x859900),
                                    .cyan                     = Color::RGB(0x2aa198),
                                    .blue                     = Color::RGB(0x268bd2),
                                    .purple                   = Color::RGB(0x6c71c4), // violet
                                    .magenta                  = Color::RGB(0xd33682),
                                    .chromeBackground         = Color::RGB(0x073642), // base02
                                    .chromeBackgroundEmphasis = Color::RGB(0x0e4b5a),
                                    .chromeForeground         = Color::RGB(0x93a1a1), // base1
                                    .border                   = Color::RGB(0x586e75),
                                    .accent                   = Color::RGB(0x6c71c4),
                                    .selectionBackground      = Color::RGB(0x073642),
                                    .searchMatchBackground    = Color::RGB(0x5a4f10),
                                });
    }

    Theme SolarizedLightTheme() {
        return ThemeFromPalette("solarized-light",
                                ThemePalette{
                                    .background               = Color::RGB(0xfdf6e3), // base3
                                    .foreground               = Color::RGB(0x657b83), // base00
                                    .subtleForeground         = Color::RGB(0x93a1a1), // base1
                                    .red                      = Color::RGB(0xdc322f),
                                    .orange                   = Color::RGB(0xcb4b16),
                                    .yellow                   = Color::RGB(0xb58900),
                                    .green                    = Color::RGB(0x859900),
                                    .cyan                     = Color::RGB(0x2aa198),
                                    .blue                     = Color::RGB(0x268bd2),
                                    .purple                   = Color::RGB(0x6c71c4),
                                    .magenta                  = Color::RGB(0xd33682),
                                    .chromeBackground         = Color::RGB(0xeee8d5), // base2
                                    .chromeBackgroundEmphasis = Color::RGB(0xe0dac6),
                                    .chromeForeground         = Color::RGB(0x586e75), // base01
                                    .border                   = Color::RGB(0xc5bfab),
                                    .accent                   = Color::RGB(0x6c71c4),
                                    .selectionBackground      = Color::RGB(0xeee8d5),
                                    .searchMatchBackground    = Color::RGB(0xf0dfa0),
                                });
    }

    // Gruvbox, Pavel Pertsev (morhetz) -- https://github.com/morhetz/gruvbox
    // (MIT). Dark uses the bright accent row, light the faded one, matching
    // upstream's own contrast guidance.
    Theme GruvboxDarkTheme() {
        return ThemeFromPalette("gruvbox-dark",
                                ThemePalette{
                                    .background               = Color::RGB(0x282828),
                                    .foreground               = Color::RGB(0xebdbb2),
                                    .subtleForeground         = Color::RGB(0x928374),
                                    .red                      = Color::RGB(0xfb4934),
                                    .orange                   = Color::RGB(0xfe8019),
                                    .yellow                   = Color::RGB(0xfabd2f),
                                    .green                    = Color::RGB(0xb8bb26),
                                    .cyan                     = Color::RGB(0x8ec07c), // aqua
                                    .blue                     = Color::RGB(0x83a598),
                                    .purple                   = Color::RGB(0xd3869b),
                                    .magenta                  = Color::RGB(0xb16286),
                                    .chromeBackground         = Color::RGB(0x1d2021), // bg0_h
                                    .chromeBackgroundEmphasis = Color::RGB(0x3c3836), // bg1
                                    .chromeForeground         = Color::RGB(0xebdbb2),
                                    .border                   = Color::RGB(0x504945), // bg2
                                    .accent                   = Color::RGB(0xfe8019),
                                    .selectionBackground      = Color::RGB(0x504945),
                                    .searchMatchBackground    = Color::RGB(0x665c1e),
                                });
    }

    Theme GruvboxLightTheme() {
        return ThemeFromPalette("gruvbox-light",
                                ThemePalette{
                                    .background               = Color::RGB(0xfbf1c7),
                                    .foreground               = Color::RGB(0x3c3836),
                                    .subtleForeground         = Color::RGB(0x928374),
                                    .red                      = Color::RGB(0x9d0006),
                                    .orange                   = Color::RGB(0xaf3a03),
                                    .yellow                   = Color::RGB(0xb57614),
                                    .green                    = Color::RGB(0x79740e),
                                    .cyan                     = Color::RGB(0x427b58),
                                    .blue                     = Color::RGB(0x076678),
                                    .purple                   = Color::RGB(0x8f3f71),
                                    .magenta                  = Color::RGB(0xb16286),
                                    .chromeBackground         = Color::RGB(0xebdbb2), // bg1
                                    .chromeBackgroundEmphasis = Color::RGB(0xd5c4a1), // bg2
                                    .chromeForeground         = Color::RGB(0x3c3836),
                                    .border                   = Color::RGB(0xbdae93),
                                    .accent                   = Color::RGB(0xaf3a03),
                                    .selectionBackground      = Color::RGB(0xd5c4a1),
                                    .searchMatchBackground    = Color::RGB(0xe9d58a),
                                });
    }

    // Nord -- https://github.com/nordtheme/nord (MIT). Frost/aurora rows for
    // the accents; the official #616e88 comment tone for subtle rather than
    // nord3, which upstream itself flags as too dim for text.
    Theme NordTheme() {
        return ThemeFromPalette("nord",
                                ThemePalette{
                                    .background               = Color::RGB(0x2e3440), // nord0
                                    .foreground               = Color::RGB(0xd8dee9), // nord4
                                    .subtleForeground         = Color::RGB(0x616e88),
                                    .red                      = Color::RGB(0xbf616a), // nord11
                                    .orange                   = Color::RGB(0xd08770), // nord12
                                    .yellow                   = Color::RGB(0xebcb8b), // nord13
                                    .green                    = Color::RGB(0xa3be8c), // nord14
                                    .cyan                     = Color::RGB(0x88c0d0), // nord8
                                    .blue                     = Color::RGB(0x81a1c1), // nord9
                                    .purple                   = Color::RGB(0xb48ead), // nord15
                                    .magenta                  = Color::RGB(0xb48ead),
                                    .chromeBackground         = Color::RGB(0x272c36),
                                    .chromeBackgroundEmphasis = Color::RGB(0x3b4252), // nord1
                                    .chromeForeground         = Color::RGB(0xeceff4), // nord6
                                    .border                   = Color::RGB(0x4c566a), // nord3
                                    .accent                   = Color::RGB(0x88c0d0),
                                    .selectionBackground      = Color::RGB(0x434c5e), // nord2
                                    .searchMatchBackground    = Color::RGB(0x665c33),
                                });
    }

    // Dracula -- https://github.com/dracula/dracula-theme (MIT). No true
    // blue in the palette: the blue slot takes purple, so keywords come out
    // purple-bold -- adjacent to upstream's pink-keyword look.
    Theme DraculaTheme() {
        return ThemeFromPalette("dracula",
                                ThemePalette{
                                    .background               = Color::RGB(0x282a36),
                                    .foreground               = Color::RGB(0xf8f8f2),
                                    .subtleForeground         = Color::RGB(0x6272a4), // comment
                                    .red                      = Color::RGB(0xff5555),
                                    .orange                   = Color::RGB(0xffb86c),
                                    .yellow                   = Color::RGB(0xf1fa8c),
                                    .green                    = Color::RGB(0x50fa7b),
                                    .cyan                     = Color::RGB(0x8be9fd),
                                    .blue                     = Color::RGB(0xbd93f9),
                                    .purple                   = Color::RGB(0xbd93f9),
                                    .magenta                  = Color::RGB(0xff79c6), // pink
                                    .chromeBackground         = Color::RGB(0x21222c),
                                    .chromeBackgroundEmphasis = Color::RGB(0x343746),
                                    .chromeForeground         = Color::RGB(0xf8f8f2),
                                    .border                   = Color::RGB(0x44475a),
                                    .accent                   = Color::RGB(0xbd93f9),
                                    .selectionBackground      = Color::RGB(0x44475a),
                                    .searchMatchBackground    = Color::RGB(0x605c2c),
                                });
    }

    // Classic Monokai, Wimer Hazenberg (the original TextMate palette, as
    // universally redistributed -- monokai.pro's successors are separate,
    // commercial works and deliberately not copied here). No blue either:
    // the blue slot takes cyan, keeping keywords in the cyan-blue family.
    Theme MonokaiTheme() {
        return ThemeFromPalette("monokai",
                                ThemePalette{
                                    .background               = Color::RGB(0x272822),
                                    .foreground               = Color::RGB(0xf8f8f2),
                                    .subtleForeground         = Color::RGB(0x75715e),
                                    .red                      = Color::RGB(0xf92672),
                                    .orange                   = Color::RGB(0xfd971f),
                                    .yellow                   = Color::RGB(0xe6db74),
                                    .green                    = Color::RGB(0xa6e22e),
                                    .cyan                     = Color::RGB(0x66d9ef),
                                    .blue                     = Color::RGB(0x66d9ef),
                                    .purple                   = Color::RGB(0xae81ff),
                                    .magenta                  = Color::RGB(0xf92672),
                                    .chromeBackground         = Color::RGB(0x1e1f1a),
                                    .chromeBackgroundEmphasis = Color::RGB(0x34352e),
                                    .chromeForeground         = Color::RGB(0xf8f8f2),
                                    .border                   = Color::RGB(0x49483e),
                                    .accent                   = Color::RGB(0xfd971f),
                                    .selectionBackground      = Color::RGB(0x49483e),
                                    .searchMatchBackground    = Color::RGB(0x5e5a2e),
                                });
    }

    // Atom One Dark / One Light -- https://github.com/atom (MIT). Same
    // values ThemePaletteTest's own anonymous sample palettes were modeled
    // on, now registered as the real themes.
    Theme OneDarkTheme() {
        return ThemeFromPalette("one-dark",
                                ThemePalette{
                                    .background               = Color::RGB(0x282c34),
                                    .foreground               = Color::RGB(0xabb2bf),
                                    .subtleForeground         = Color::RGB(0x5c6370),
                                    .red                      = Color::RGB(0xe06c75),
                                    .orange                   = Color::RGB(0xd19a66),
                                    .yellow                   = Color::RGB(0xe5c07b),
                                    .green                    = Color::RGB(0x98c379),
                                    .cyan                     = Color::RGB(0x56b6c2),
                                    .blue                     = Color::RGB(0x61afef),
                                    .purple                   = Color::RGB(0xc678dd),
                                    .magenta                  = Color::RGB(0xc678dd),
                                    .chromeBackground         = Color::RGB(0x21252b),
                                    .chromeBackgroundEmphasis = Color::RGB(0x2c313a),
                                    .chromeForeground         = Color::RGB(0xd7dae0),
                                    .border                   = Color::RGB(0x3e4452),
                                    .accent                   = Color::RGB(0x528bff),
                                    .selectionBackground      = Color::RGB(0x3e4452),
                                    .searchMatchBackground    = Color::RGB(0x6b5d24),
                                });
    }

    Theme OneLightTheme() {
        return ThemeFromPalette("one-light",
                                ThemePalette{
                                    .background               = Color::RGB(0xfafafa),
                                    .foreground               = Color::RGB(0x383a42),
                                    .subtleForeground         = Color::RGB(0xa0a1a7),
                                    .red                      = Color::RGB(0xe45649),
                                    .orange                   = Color::RGB(0x986801),
                                    .yellow                   = Color::RGB(0xc18401),
                                    .green                    = Color::RGB(0x50a14f),
                                    .cyan                     = Color::RGB(0x0184bc),
                                    .blue                     = Color::RGB(0x4078f2),
                                    .purple                   = Color::RGB(0xa626a4),
                                    .magenta                  = Color::RGB(0xa626a4),
                                    .chromeBackground         = Color::RGB(0xeaeaeb),
                                    .chromeBackgroundEmphasis = Color::RGB(0xdbdbdc),
                                    .chromeForeground         = Color::RGB(0x383a42),
                                    .border                   = Color::RGB(0xd4d4d5),
                                    .accent                   = Color::RGB(0x526fff),
                                    .selectionBackground      = Color::RGB(0xcfd8e8),
                                    .searchMatchBackground    = Color::RGB(0xf2d54c),
                                });
    }

    // Catppuccin -- https://github.com/catppuccin/catppuccin (MIT). Mocha
    // (darkest) and Latte (light); Frappe/Macchiato are stretch flavors,
    // deferred (see ROADMAP).
    Theme CatppuccinMochaTheme() {
        return ThemeFromPalette("catppuccin-mocha",
                                ThemePalette{
                                    .background               = Color::RGB(0x1e1e2e), // base
                                    .foreground               = Color::RGB(0xcdd6f4), // text
                                    .subtleForeground         = Color::RGB(0x6c7086), // overlay0
                                    .red                      = Color::RGB(0xf38ba8),
                                    .orange                   = Color::RGB(0xfab387), // peach
                                    .yellow                   = Color::RGB(0xf9e2af),
                                    .green                    = Color::RGB(0xa6e3a1),
                                    .cyan                     = Color::RGB(0x94e2d5), // teal
                                    .blue                     = Color::RGB(0x89b4fa),
                                    .purple                   = Color::RGB(0xcba6f7), // mauve
                                    .magenta                  = Color::RGB(0xf5c2e7), // pink
                                    .chromeBackground         = Color::RGB(0x181825), // mantle
                                    .chromeBackgroundEmphasis = Color::RGB(0x313244), // surface0
                                    .chromeForeground         = Color::RGB(0xcdd6f4),
                                    .border                   = Color::RGB(0x45475a), // surface1
                                    .accent                   = Color::RGB(0xb4befe), // lavender
                                    .selectionBackground      = Color::RGB(0x45475a),
                                    .searchMatchBackground    = Color::RGB(0x66582a),
                                });
    }

    Theme CatppuccinLatteTheme() {
        return ThemeFromPalette("catppuccin-latte",
                                ThemePalette{
                                    .background               = Color::RGB(0xeff1f5), // base
                                    .foreground               = Color::RGB(0x4c4f69), // text
                                    .subtleForeground         = Color::RGB(0x9ca0b0), // overlay0
                                    .red                      = Color::RGB(0xd20f39),
                                    .orange                   = Color::RGB(0xfe640b), // peach
                                    .yellow                   = Color::RGB(0xdf8e1d),
                                    .green                    = Color::RGB(0x40a02b),
                                    .cyan                     = Color::RGB(0x179299), // teal
                                    .blue                     = Color::RGB(0x1e66f5),
                                    .purple                   = Color::RGB(0x8839ef), // mauve
                                    .magenta                  = Color::RGB(0xea76cb), // pink
                                    .chromeBackground         = Color::RGB(0xe6e9ef), // mantle
                                    .chromeBackgroundEmphasis = Color::RGB(0xdce0e8), // crust
                                    .chromeForeground         = Color::RGB(0x4c4f69),
                                    .border                   = Color::RGB(0xbcc0cc), // surface1
                                    .accent                   = Color::RGB(0x7287fd), // lavender
                                    .selectionBackground      = Color::RGB(0xccd0da),
                                    .searchMatchBackground    = Color::RGB(0xe6c890),
                                });
    }

    // Tokyo Night, enkia -- https://github.com/tokyo-night (MIT). The
    // "night" flavor plus "day"; "storm" is a stretch flavor, deferred.
    Theme TokyoNightTheme() {
        return ThemeFromPalette("tokyo-night",
                                ThemePalette{
                                    .background               = Color::RGB(0x1a1b26),
                                    .foreground               = Color::RGB(0xc0caf5),
                                    .subtleForeground         = Color::RGB(0x565f89), // comment
                                    .red                      = Color::RGB(0xf7768e),
                                    .orange                   = Color::RGB(0xff9e64),
                                    .yellow                   = Color::RGB(0xe0af68),
                                    .green                    = Color::RGB(0x9ece6a),
                                    .cyan                     = Color::RGB(0x7dcfff),
                                    .blue                     = Color::RGB(0x7aa2f7),
                                    .purple                   = Color::RGB(0x9d7cd8),
                                    .magenta                  = Color::RGB(0xbb9af7),
                                    .chromeBackground         = Color::RGB(0x16161e),
                                    .chromeBackgroundEmphasis = Color::RGB(0x292e42),
                                    .chromeForeground         = Color::RGB(0xc0caf5),
                                    .border                   = Color::RGB(0x3b4261),
                                    .accent                   = Color::RGB(0x7aa2f7),
                                    .selectionBackground      = Color::RGB(0x283457),
                                    .searchMatchBackground    = Color::RGB(0x3d59a1),
                                });
    }

    Theme TokyoNightDayTheme() {
        return ThemeFromPalette("tokyo-night-day",
                                ThemePalette{
                                    .background               = Color::RGB(0xe1e2e7),
                                    .foreground               = Color::RGB(0x3760bf),
                                    .subtleForeground         = Color::RGB(0x848cb5), // comment
                                    .red                      = Color::RGB(0xf52a65),
                                    .orange                   = Color::RGB(0xb15c00),
                                    .yellow                   = Color::RGB(0x8c6c3e),
                                    .green                    = Color::RGB(0x587539),
                                    .cyan                     = Color::RGB(0x007197),
                                    .blue                     = Color::RGB(0x2e7de9),
                                    .purple                   = Color::RGB(0x7847bd),
                                    .magenta                  = Color::RGB(0x9854f1),
                                    .chromeBackground         = Color::RGB(0xd0d5e3),
                                    .chromeBackgroundEmphasis = Color::RGB(0xc4c8da),
                                    .chromeForeground         = Color::RGB(0x3760bf),
                                    .border                   = Color::RGB(0xa8aecb),
                                    .accent                   = Color::RGB(0x2e7de9),
                                    .selectionBackground      = Color::RGB(0xb2bce2),
                                    .searchMatchBackground    = Color::RGB(0xead988),
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
        {"fuchsia", FuchsiaTheme},
        {"solarized-dark", SolarizedDarkTheme},
        {"solarized-light", SolarizedLightTheme},
        {"gruvbox-dark", GruvboxDarkTheme},
        {"gruvbox-light", GruvboxLightTheme},
        {"nord", NordTheme},
        {"dracula", DraculaTheme},
        {"monokai", MonokaiTheme},
        {"one-dark", OneDarkTheme},
        {"one-light", OneLightTheme},
        {"catppuccin-mocha", CatppuccinMochaTheme},
        {"catppuccin-latte", CatppuccinLatteTheme},
        {"tokyo-night", TokyoNightTheme},
        {"tokyo-night-day", TokyoNightDayTheme},
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
