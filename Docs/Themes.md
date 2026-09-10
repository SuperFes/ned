# Themes

What's bundled, how a theme is picked at startup, and how to author a new one.
Ground-truthed against `Source/UI/ThemeRegistry.cpp`, `Source/UI/ThemePalette.h`,
`Source/UI/DesktopThemeProbe.h`, and `Source/main.cpp`'s startup sequence.

## Bundled themes

Every name below resolves via `ThemeByName` (`Source/UI/ThemeRegistry.cpp`) -- the same
lookup `ned/set-theme`, the remembered-theme variable, and the `M-x select-theme` picker
all go through.

**Hand-built:** `dark`, `light` (the two original themes, `Theme.cpp`). Both are entirely
real RGB -- no ANSI colour names, which stopped meaning "whatever this terminal calls blue"
when the palette fallback went away and would now resolve to xterm's flat defaults.

There is no ANSI fallback pair any more, and no startup capability swap: every theme is
truecolor, a terminal that can't render one is Notcurses' problem to quantize, and a theme
is expected to hold its own BG/FG contrast rather than hand off to a palette-restricted
twin. See `Docs/Translucency.md` for why (translucency has no meaning over a palette
index). A theme file written before that change still loads -- its `x:<n>` tokens resolve
to real RGB.

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

## Semantic colour keys

Beyond the syntax classes, a handful of keys name a *meaning* rather than a language
construct, and widgets reach for those instead of hard-coding a colour:

| key | means | used by |
|---|---|---|
| `success_foreground` | this is fine | passed test, covered line, added file |
| `diagnostic_error` | this is wrong | failed test, uncovered line, deleted file, removed diff line |
| `diagnostic_warning` | this needs attention | skipped test, partially covered line |
| `vcs_modified_foreground` | changed but present | modified file, modified diff line |
| `vcs_untracked_foreground` | unknown to the repository | untracked file |
| `blame_recent_foreground` / `blame_old_foreground` | the two ends of the blame age ramp | blame gutter |
| `line_number_foreground` | a quiet gutter affordance | a discovered-but-not-yet-run test |

A theme sets these like any other colour key, and `ThemeFromPalette` derives all of them,
so a cloned theme gets them without its author writing a line.

## Paints, gradients and surfaces

Beyond the flat colour keys above, a theme can set a *paint* -- a gradient, a fade, a
pattern, or a blur -- on a named *surface*. The full design (and why translucency works the
way it does here) is `Docs/Translucency.md`; this is the authoring surface.

### The grammar, in one rule

> A paint is an optional axis or pattern keyword, then stop values, with numbers between
> them as relative weights.

Strings are stops. Numbers are weights. That is all of it.

```janet
(ned/gradient "brand" [:diag "$accent" 2 "$keyword"])   # array form (bundled sugar)
(ned/theme-gradient "brand" "diag $accent 2 $keyword")  # the same thing, one line
```

Both are equivalent: the array form is Janet sugar from the bundled `gradients.janet`
plugin, which flattens it into the one-line string the editor actually parses. The one-line
form is also what `M-x save-theme` writes and what the plain `key=value` theme file takes.

- **Axes** are `:x`, `:y`, `:diag`, `:radial`, defaulting to `:y`. Radial centres on the box.
- **Weights** are relative span sizes. `[:y "#89b4faff" 3 "#89b4fa00"]` spends three
  quarters of the run on the first colour's side. Omitted weights are 1, so an unweighted
  array is an even split. A stop at 35% is just `35 ... 65`.
- **Colour stops** are `"#rrggbb"`, `"#rrggbbaa"`, `"default"`, or a `$slot` reference.
- **Percentage stops** (`"60%"`) make it a *fade*: it modulates whatever colour is already
  there instead of replacing it, which is what you want over code. Mixing colour and
  percentage stops in one paint is an error.
- **Patterns** put a pattern keyword where the axis went, and may take one number -- their
  period in cells. Every other number is still a weight, read as duty cycle:
  `[:stripes 6 "$bg" 3 "$accent/18"]`. The set is `:checker`, `:stripes`, `:scanlines`,
  `:grid`, `:hatch`, `:dots`, `:noise` (whose period is a jitter amplitude in percent).
  Patterns decline cells that carry a glyph, so they texture a surface's empty space rather
  than eating text.
- **`:blur`** takes a radius and an optional tint: `[:blur 2 "#00000033"]`. It samples what
  is beneath and box-blurs it.
- **`:stack`** applies several paints in order: `[:stack "$glass" [:noise 6 "$bg"]]`. It is
  the one construct with no one-line form, so a stacked paint cannot be written into the
  flat theme file (`save-theme` says so in a comment rather than emitting something that
  would mean the wrong thing).

### `$slot` references

A stop may name one of the theme's own colours instead of a literal, with up to three
postfix adjustments applied left to right:

```
$bg          $bg+8         $accent-12         $keyword/60
slot         lighten 8%    darken 12%         60% alpha
```

Slots: `bg`, `fg`, `subtle`, `selection`, `search`, `accent`, `desktop-accent`, `border`,
`chrome-bg`, `chrome-fg`, `error`, `warning`, `info`, `hint`, `added`, `removed`,
`comment`, `string`, `keyword`, `number`, `type`, `function`.

`desktop-accent` is the odd one out: every other slot reads the *active theme*, but this is
the accent your desktop (or terminal) reported at startup, so it keeps meaning "the colour
my desktop uses" even after switching to a bundled theme with an accent of its own. If
nothing was detected it falls back to the theme's `accent`, so a paint using it always
resolves. Anything more expressive than lighten/darken/alpha
is Janet's job -- it is a real language, and a theme that wants a computed colour can
compute one.

A stop naming a *paint* expands to that paint's own stops, which is what makes presets
building blocks rather than terminal values:

```janet
(ned/gradient "header"  [:x "$spectrum"])                 # the preset, re-axed
(ned/gradient "tabfade" [:y "$brand" 3 "$bg/0"])          # extended with a settling stop
```

### Surfaces

```janet
(ned/surface "popup" :fill [:y "$bg/78" 3 "$bg/52"] :border "$brand")
(ned/theme-surface "popup" "fill" "y $bg/78 3 $bg/52")    # one part, one line
```

Parts are `fill`, `border` and `text`. Names: `buffer`, `buffer.current_line`,
`buffer.selection`, `buffer.search`, `modeline`, `modeline.focused`, `tab`, `tab.active`,
`echo`, `scrollbar`, `panel`, `popup`.

Surfaces are **additive**: anything a theme does not set stays derived from the colour keys
above, so setting none of them leaves every widget painting exactly what it always has.
`buffer.current_line` defaults to the detected desktop accent at ~16% (the focused mode
line's accent when nothing has been detected), which is deliberately below every other
overlay: it is up the whole time you are typing, so a selection or a search hit has to win
against it rather than tie. It sets `fill` alone -- a current-line marker is a background,
and the rest of the line's colour is the theme's own business.

### Translucent overlays

Selection, search-match and snippet-field backgrounds are composited over the buffer's own
background rather than replacing it, so any of them can carry alpha:

```janet
(ned/theme-set "selection_background" "#61afef80")   # the accent at half strength
```

An opaque value behaves exactly as it always did. A detected theme uses this already: its
selection is the desktop accent at 50%, its mode line fades toward the buffer as it runs
right, and an inactive tab sits back behind the active one. Over a theme whose background
is the terminal's own there is nothing to composite against, so a translucent overlay lands
opaque -- see `Docs/Translucency.md` for the two-pass layering that would fix that.

### Bundled presets

`gradients.janet` ships a preset set, loaded before `init.janet` so redefining one by name
simply wins. Palette-derived (these follow any theme): `lift`, `sink`, `glass`, `edge`,
`scrim`, `focus`, `brand`, `rule`. Signature ramps (absolute colours, deliberately loud):
`spectrum`, `aurora`, `sunset`, `ember`, `ice`, `vapor`, `ink`. Fades: `vanish`, `ghost`.
Patterns: `grain`, `scanlines`, `checker`, `hatch`, `graph`, `stipple`.

A spectrum behind code is unreadable, so the loud ones belong on chrome that owns its own
row or box -- a mode line, a tab strip, a popup border.

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

`DarkTheme()`/`LightTheme()` are hand-built `Theme` literals instead (`Theme.cpp`), not
palette-derived -- they predate `ThemePalette` and stay as they are; new themes should go
through `ThemeFromPalette` rather than hand-filling all ~70 `Theme` fields directly.
