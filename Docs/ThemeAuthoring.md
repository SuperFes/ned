# Authoring a Bundled Theme

How to add a new theme to ned's own bundled set (`Source/UI/ThemeRegistry.cpp`), as
opposed to a user writing `ned/theme-set` overrides in their own `init.janet` -- see
`Docs/Themes.md` for that end-user surface.

Nearly every published theme spec (base16, Solarized's own table, Catppuccin's, Gruvbox's,
...) is a background/foreground pair plus a handful of accent hues -- that's exactly the
`ThemePalette` struct (`Source/UI/ThemePalette.h`): `background`, `foreground`,
`subtleForeground` (comments, punctuation, line numbers, anything that should read as
receded), eight named-by-hue accent slots (`red`/`orange`/`yellow`/`green`/`cyan`/`blue`/
`purple`/`magenta` -- the *role* each hue plays, e.g. keyword vs. string, is fixed once in
`ThemeFromPalette` and applies identically to every theme, only the hues differ per
palette), and a small UI-chrome group (`chromeBackground`/`chromeBackgroundEmphasis`/
`chromeForeground`/`border`/`accent`/`selectionBackground`/`searchMatchBackground`).

To add a clone of a real published theme:

1. Add a `Theme <Name>Theme() { return ThemeFromPalette("<name>", ThemePalette{...}); }`
   factory in `Source/UI/ThemeRegistry.cpp`'s anonymous namespace, transcribing the
   upstream project's own published hex values into the slots above. A palette with no
   real distinct hue for a slot reuses its nearest neighbor (e.g. Dracula has no true blue,
   so its `blue` slot reuses `purple`) -- note the substitution in a comment, the same way
   every existing clone does.
2. Add a comment above the factory naming the upstream project, its repository URL, and
   its license (every clone bundled today is MIT or, for classic Monokai, universally
   redistributed -- color palettes themselves are uncopyrightable facts, but attribution is
   kept anyway as standard editor practice).
3. Register the name in `kThemeFactories`.
4. Add the name to `Tests/BundledThemesTest.cpp`'s clone list (`RequireForegroundContrast`
   with a floor of 40 is the automated black-on-black guard every bundled theme must
   clear) and to `Tools/theme-sweep.sh`'s `kThemeNames` (both are hand-kept copies of the
   registry table, not generated from it).
5. Run `Tools/theme-sweep.sh` and eyeball the capture for the new theme -- the automated
   contrast floor catches genuinely broken pairings, not "does this actually look right."

For a theme that isn't a clone of anything published (like `fuchsia`), the same
`ThemePalette` authoring surface applies -- just choose values directly rather than
transcribing them from an upstream source.

`DarkTheme()`/`LightTheme()` are hand-built `Theme` literals instead (`Theme.cpp`), not
palette-derived -- they predate `ThemePalette` and stay as they are; new themes should go
through `ThemeFromPalette` rather than hand-filling all ~70 `Theme` fields directly.
