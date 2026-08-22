# Themes

What's bundled, how a theme is picked at startup, and how to author a new one.
Ground-truthed against `Source/UI/ThemeRegistry.cpp`, `Source/UI/ThemePalette.h`,
`Source/UI/DesktopThemeProbe.h`, and `Source/main.cpp`'s startup sequence.

## Bundled themes

Every name below resolves via `ThemeByName` (`Source/UI/ThemeRegistry.cpp`) -- the same
lookup `ned/set-theme`, the remembered-theme variable, and the `M-x select-theme` picker
all go through.

**Hand-built:** `dark`, `light` (the two original themes, `Theme.cpp`), `ansi-dark`,
`ansi-light` (Palette16-only variants for terminals without truecolor/256-color support --
selected automatically at startup instead of `dark`/`light` when the terminal can't do
better, see `Theme.h`'s `AnsiFallbackFor`).

**Palette-derived originals:** `major-dark`/`major-light` (vivid saturated primaries),
`minor-dark`/`minor-light` (muted/pastel), `high-contrast-dark`/`high-contrast-light`
(pure black/white backgrounds, held to a raised contrast floor), `mono-dark`/`mono-light`
(genuinely grayscale -- role separation comes from luminance steps and Brush bold/italic,
not hue), `fuchsia` (a dark theme built around one signature hue).

**Cloned themes** (each a transcription of its upstream project's published palette into
`ThemePalette` slots -- see `ThemeRegistry.cpp`'s own attribution comment on each factory
function for the exact source and any "no true X, nearest neighbor is Y" hue substitution):
`solarized-dark`/`solarized-light`, `gruvbox-dark`/`gruvbox-light`, `nord`, `dracula`,
`monokai`, `one-dark`/`one-light`, `catppuccin-mocha`/`catppuccin-latte`/
`catppuccin-frappe`/`catppuccin-macchiato`, `tokyo-night`/`tokyo-night-day`/
`tokyo-night-storm`, `rose-pine`, `everforest`, `zenburn`.

Color palettes are uncopyrightable facts; every upstream project cloned here ships under
MIT (or, for classic Monokai, is universally redistributed), and names are kept as-is per
universal editor practice.

## Picking a theme

- **`M-x select-theme`** -- fuzzy-filtered live picker (type to narrow, arrows to move,
  every highlight change previews immediately). `Enter` commits: the choice is written to
  `$XDG_STATE_HOME/ned/variables.json` (the `"theme"` variable) so it survives restarts.
  `C-g` cancels and restores whatever was active before the picker opened, without writing
  anything.
- **`(ned/set-theme "name")`** in `init.janet` -- a static default. Loses to a
  variables.json-remembered pick (see precedence below), so once you've committed a choice
  through the picker, this line stops having any visible effect until you clear the
  remembered variable.
- **`(ned/theme-set "key" "value")`** -- overrides one field of whichever base theme wins,
  applied last regardless of how the base was chosen. `M-x save-theme` writes a full
  `theme.janet` of these calls for the *current* live theme (one call per color/trait),
  meant to be hand-edited and loaded from `init.janet` via `(dofile ...)`. Keys match the
  plain-text theme file's own key names (`keyword_foreground`, `active_tab_bold`, ...);
  color values are `"#rrggbb"` or `"default"`, trait values are `"true"`/`"false"`.
- **`ned --detect-theme [--transparent] [output-path]`** -- a separate CLI mode, not part
  of a normal launch. Probes the *terminal's* actual configured colors (OSC 10/11/4 --
  foreground, background, and the 16-slot ANSI palette) and writes a theme file (default
  path: the same one loaded at startup, see below), then exits without starting the editor
  at all. Must be run and finish before ned's own event loop starts reading stdin, which is
  why it's a distinct invocation rather than something that could run automatically on
  every launch -- see `Source/UI/TerminalColorProbe.h`'s header comment for exactly why.
  `--transparent` treats the detected background as transparent (`Color::Default`) instead
  of the queried opaque color.

## Startup precedence

In order, first match wins (an unresolvable name at any step falls through to the next
source rather than aborting, reported via the status line):

1. **The remembered `"theme"` variable** (`$XDG_STATE_HOME/ned/variables.json`) --
   whatever `M-x select-theme` last committed. The newest expression of intent, so it beats
   even a static `init.janet` `(ned/set-theme ...)` call.
2. **`(ned/set-theme "name")`** from `init.janet`.
3. **A previously `ned --detect-theme`-generated file**, if one exists at the default theme
   file path. Never probes the terminal itself on a normal launch -- only reads a file that
   `--detect-theme` already wrote out in an earlier, separate invocation.
4. **A live desktop-environment probe** (`Source/UI/DesktopThemeProbe.h`) -- queries the
   running desktop for its light/dark preference and accent color, cheaply and without
   touching terminal state, so unlike step 3 this runs unconditionally on every normal
   launch that reaches this point. See "Desktop-environment detection" below.
5. **`DarkTheme()`**, the fixed final default.

Regardless of which base wins, every `(ned/theme-set ...)` override from `init.janet`
still applies last, on top of it -- overrides always determine the final look, even over a
`--detect-theme` file or a desktop-detected base.

## Desktop-environment detection

Step 4 above tries, in order, until both a polarity (light/dark) and an accent color are
known or every option is exhausted:

1. **The freedesktop `org.freedesktop.appearance` portal setting**, over D-Bus via
   whichever of `gdbus`/`busctl` is on `$PATH`. Deliberately desktop-agnostic -- this is
   tried first regardless of which desktop is actually running, and covers GNOME and any
   Plasma version new enough to ship `xdg-desktop-portal-kde`'s appearance implementation
   without this code needing to know which one it's talking to.
2. **GNOME's own `gsettings` keys** (`org.gnome.desktop.interface color-scheme` and
   `accent-color`), tried only if `$XDG_CURRENT_DESKTOP`/`$XDG_SESSION_DESKTOP` names GNOME
   and the portal didn't answer everything. The accent name is mapped through Adwaita's
   fixed nine-color palette.
3. **A direct parse of `kdeglobals`**, tried only if the desktop names KDE/Plasma and the
   portal didn't answer everything. Polarity comes from `[General]`'s `ColorScheme=` name
   containing "dark"/"light"; accent comes from `[General]`'s `AccentColor=` (Plasma 6),
   falling back to `[Colors:Selection]`'s `BackgroundNormal=` (the selection-highlight
   color, which is the accent in practice for schemes that don't set `AccentColor`
   explicitly).

Every step is independently optional -- a missing tool, an unreadable file, or an
unparseable reply just leaves that one fact undetermined, never a hard failure. If nothing
at all could be determined, step 4 is skipped entirely and step 5 (`DarkTheme()`) applies.
When only polarity or only an accent was found, the other half falls back to `DarkTheme()`'s
own default (dark, no accent override).

The derived theme is `DarkTheme()`/`LightTheme()` by polarity, with a found accent color
applied to the same fields `--detect-theme`'s own single-accent case applies to (the border
accent, the keyword syntax color, and the focused mode-line gradient blended 60% toward
it) -- one detected color still produces a coherent-looking theme rather than a literal,
half-derived one.

## Authoring a new theme

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

`DarkTheme()`/`LightTheme()` and the ANSI fallback pair are hand-built `Theme` literals
instead (`Theme.cpp`), not palette-derived -- they predate `ThemePalette` and stay as they
are; new themes should go through `ThemeFromPalette` rather than hand-filling all ~70
`Theme` fields directly.
