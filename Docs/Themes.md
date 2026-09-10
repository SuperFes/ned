# Themes

What's bundled, how a theme is picked at startup, and how to author a new one.
Ground-truthed against `Source/UI/ThemeRegistry.cpp`, `Source/UI/ThemePalette.h`,
`Source/UI/DesktopThemeProbe.h`, and `Source/main.cpp`'s startup sequence.

## Bundled themes

Every name below resolves via `ThemeByName` (`Source/UI/ThemeRegistry.cpp`) -- the same
lookup `ned/set-theme` and the `M-x select-theme` picker both go through.

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

## Setting a theme

- **`(ned/set-theme "name")`** in `init.janet` -- picks the base theme. A project's
  `<root>/.ned/init.janet` loads after your global one, so it wins for that project.

  Names resolve leniently: case-insensitive, with spaces and underscores equal to hyphens,
  so `"Gruvbox Dark"`, `"gruvbox-dark"` and `"GRUVBOX_DARK"` are the same theme. The picker
  lists the display form; `Theme::name` and every other surface use the hyphenated one.
- **`(ned/theme-set "key" "value")`** -- overrides one field of whichever base theme wins,
  applied last regardless of how the base was chosen. This is how you write your own theme:
  put the calls in `init.janet`, on top of whichever bundled theme `ned/set-theme` picks.
  Colour values are `"#rrggbb"`, `"#rrggbbaa"` or `"default"`; trait values are `"true"` /
  `"false"`. Every key is listed under "Key reference" below.

## Writing your own theme

There is no theme file format and no generator: a theme is `ned/theme-set` calls in your
own `init.janet`, over a bundled base. That is deliberate -- with 30 bundled themes and
`ThemeFromPalette` deriving a full theme from ~15 semantic colours, a theme worth writing
is a handful of overrides, not a 117-key snapshot.

```janet
(ned/set-theme "gruvbox-dark")                        # the base
(ned/theme-set "keyword_foreground"   "#83a598")      # ...and what you disagree with
(ned/theme-set "selection_background" "#45403d80")    # alpha composites, see below
(ned/theme-set "active_tab_bold"      "true")
```

`M-x theme-gallery` is the discovery surface for the *paint* vocabulary below -- surfaces
and named paints resolve against the live theme, so unlike a colour key they cannot be read
off a page. It shows every surface and named paint as a live swatch with a contrast
readout, and updates as the theme changes.

## Key reference

Every key `ned/theme-set` accepts. `Tests/ThemeKeyDocsTest.cpp` holds this list against the
real table in both directions, so a key cannot be added without appearing here and a key
cannot linger here after it stops existing.

<!-- theme-keys:begin -->

**Base**

`background` `default_foreground`

**Syntax**

`comment_foreground` `doc_comment_foreground` `string_foreground` `string_escape_foreground`
`keyword_foreground` `control_keyword_foreground` `keyword_modifier_foreground` `number_foreground`
`function_foreground` `function_builtin_foreground` `method_foreground` `constructor_foreground`
`type_foreground` `type_builtin_foreground` `return_type_foreground` `constant_foreground`
`constant_builtin_foreground` `variable_foreground` `variable_builtin_foreground` `parameter_foreground`
`property_foreground` `operator_foreground` `punctuation_foreground` `tag_foreground`
`attribute_foreground` `namespace_foreground` `label_foreground` `include_path_foreground`
`markup_marker_foreground`

**Mode line and gutter**

`mode_line_foreground` `mode_line_gradient_start` `mode_line_gradient_end` `mode_line_focused_gradient_start`
`mode_line_focused_gradient_end` `line_number_foreground` `current_line_number_foreground` `indent_guide_foreground`

**Overlays (all composited, so all accept alpha)**

`selection_background` `isearch_match_background` `snippet_field_background` `document_highlight_background`
`conflict_ours_background` `conflict_theirs_background` `conflict_base_background` `execution_line_background`
`diff_added_background` `diff_removed_background` `trailing_whitespace_background`

**Diagnostics**

`diagnostic_error` `diagnostic_warning` `diagnostic_information` `diagnostic_hint`

**Debugger**

`breakpoint_marker` `unverified_breakpoint_marker` `execution_marker`

**Version control**

`vcs_modified_foreground` `vcs_untracked_foreground` `blame_recent_foreground` `blame_old_foreground`
`unsaved_change_indicator` `success_foreground`

**Org and Markdown**

`headline_level1_foreground` `headline_level2_foreground` `headline_level3_foreground` `todo_keyword_foreground`
`done_keyword_foreground` `checkbox_foreground`

**Misc text**

`binary_foreground` `ghost_text_foreground` `link_foreground` `truncation_indicator_foreground`
`underline_foreground` `strikethrough_foreground`

**Brush-valued fields.** Each of these names a foreground/background pair plus four
style flags, so each expands to six keys -- `<prefix>_background`, `<prefix>_foreground`,
`<prefix>_bold`, `<prefix>_italic`, `<prefix>_underlined`, `<prefix>_strikethrough`.
The colour keys take a colour token; the four flags take `"true"` or `"false"`.

`echo_area_background` `echo_area_foreground` `echo_area_bold` `echo_area_italic` `echo_area_underlined` `echo_area_strikethrough`
`tab_bar_background` `tab_bar_foreground` `tab_bar_bold` `tab_bar_italic` `tab_bar_underlined` `tab_bar_strikethrough`
`active_tab_background` `active_tab_foreground` `active_tab_bold` `active_tab_italic` `active_tab_underlined` `active_tab_strikethrough`
`scroll_bar_background` `scroll_bar_foreground` `scroll_bar_bold` `scroll_bar_italic` `scroll_bar_underlined` `scroll_bar_strikethrough`
`scroll_bar_disabled_background` `scroll_bar_disabled_foreground` `scroll_bar_disabled_bold` `scroll_bar_disabled_italic` `scroll_bar_disabled_underlined` `scroll_bar_disabled_strikethrough`
`border_background` `border_foreground` `border_bold` `border_italic` `border_underlined` `border_strikethrough`
`border_accent_background` `border_accent_foreground` `border_accent_bold` `border_accent_italic` `border_accent_underlined` `border_accent_strikethrough`

<!-- theme-keys:end -->

## Startup precedence

One rule: **a theme is whatever a config file says.** Nothing else decides it.

1. **`(ned/set-theme "name")`** -- from a project's `<root>/.ned/init.janet` if it has one,
   else your global `init.janet`. The project file loads second, so it wins.
2. **A live desktop-environment probe** (`Source/UI/DesktopThemeProbe.h`) -- queries the
   running desktop for its light/dark preference and accent colour, cheaply and without
   touching terminal state, so it runs on every launch that reaches this point. If you never
   name a theme, this is what you get.
3. **`DarkTheme()`**, the fixed final default.

Then every `(ned/theme-set ...)` override applies on top, in call order, so a later call for
the same key wins and a field you set by hand always sticks.

There used to be two more sources above `ned/set-theme`, and both are gone:

- A `theme.txt` cache written by `ned --detect-theme`, which queried the *terminal's* own
  colours over OSC. That probe had to run before anything read stdin, so it could never be
  part of an ordinary launch -- it needed a cache file plus a separate invocation to be
  useful at all, and a cache goes stale. The desktop probe has none of those problems.
- A remembered `"theme"` variable in `$XDG_STATE_HOME`, written by the picker, which
  outranked `ned/set-theme` outright. That made `(ned/set-theme "nord")` in an init.janet
  appear to be *ignored* once you had picked something else, with nothing on screen to say
  why -- an implicit pin quietly overruling an explicit setting. `M-x select-theme` applies
  for the session now and offers to write the line down; see below.

## Picking a theme, and keeping it

`M-x select-theme` previews live as you move, and `Enter` applies the highlighted theme --
**for this session**. It then asks whether to write it down:

```
Theme: Nord. Write (ned/set-theme "nord") to init.janet? (y/n)
```

`y` writes into your **global** `init.janet`; `n` leaves the theme applied and the file
untouched. Trying a theme and deciding to keep one are different acts, and ned never edits a
config file without being asked.

The write is deliberately narrow: it replaces the last line that is exactly a
`(ned/set-theme "...")` call, preserving its indentation, and otherwise appends one at the
end. It never reformats, reorders, or touches a line it did not match, and it preserves the
file's permissions. A call it does not recognise -- one inside a conditional, or sharing a
line with other code -- is left alone and a plain call is appended instead; that is correct
rather than merely cautious, since a later `ned/set-theme` is the one that takes effect.

Two rows sit above the themes and are not themes:

- **`Current theme`** -- leave everything as it is. It is what the picker opens on, so
  opening the picker never disturbs the active look (in particular it never strips
  `init.janet`'s own `ned/theme-set` overrides, which a lookup by name would).
- **`None (detect)`** -- show what this editor looks like with no theme configured at all,
  i.e. the desktop probe's answer. It cannot write that down for you, since "no
  `ned/set-theme` line" is not a line to add -- delete yours to keep it.

### Per-project themes

A project can carry its own `<root>/.ned/init.janet`:

```janet
# <project>/.ned/init.janet
(ned/set-theme "gruvbox-light")
(ned/theme-set "selection_background" "#45403d80")
```

It loads after your global `init.janet`, so it wins for anything both set, and it is a
checked-in file -- so the whole team gets the same look. The picker deliberately does not
write here: which project should look different is a decision worth making on purpose, not
a side effect of trying a theme once.

A project init file is arbitrary code triggered by opening a directory, so it is never run
silently: the first time you open a project -- and again whenever the file's content changes
-- ned prompts before running it. Answering yes loads it and re-resolves the theme
immediately, so a project theme takes effect on the spot rather than on the next launch.

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
applied to the same fields a single detected accent has always applied to (the border
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
form is the only one C++ parses; the array form is sugar the bundled `gradients.janet`
flattens into it.

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
  the one construct with no one-line form, so it is only expressible in the array form --
  which is why a stack's layers are usually *named* paints.

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
`buffer.selection`, `buffer.search`, `modeline`, `modeline.focused`, `tab.strip`, `tab`,
`tab.active`, `tab.active.focused`, `echo`, `scrollbar`, `panel`, `popup`.

Not all of them are painted yet. Setting one no widget consumes parses and stores fine and
then does nothing visible -- `buffer`, `buffer.selection`, `buffer.search`, `echo`,
`scrollbar` and `popup` are in that state today, waiting on the text-layer and popup phases
in `ROADMAP.md`. `M-x theme-gallery` lists every name either way, since what it shows is
what the surface *resolves to*, not whether anything draws it.

Surfaces are **additive**: anything a theme does not set stays derived from the colour keys
above, so setting none of them leaves every widget painting exactly what it always has.
`buffer.current_line` defaults to the detected desktop accent at ~16% (the focused mode
line's accent when nothing has been detected), which is deliberately below every other
overlay: it is up the whole time you are typing, so a selection or a search hit has to win
against it rather than tie. It sets `fill` alone -- a current-line marker is a background,
and the rest of the line's colour is the theme's own business.

`panel` is the other derived default that is not simply the flat colour its widgets used
to paint: it is a horizontal walk, ~4% lifted at the left dock's outer edge and settling to
exactly the buffer background where the two meet. That gives the dock a direction without
putting a step in the seam, and it is why the dock reads as lifting away from the buffer
rather than butting against it. The direction follows the background's own luminance --
lighter on a dark theme, darker on a light one -- because a fixed "toward white" does
nothing on a light theme, whose background already sits a couple of levels off white.
`LeftDock`, `ProjectSidebar` and `VcsPanel` all paint through it, so the rail, the border
columns and the panel interior ramp together.

The falloff runs on the *panel's* side of the seam and never the buffer's: the buffer's
columns carry code, and a wash over a glyph tints the text rather than its background (see
`Docs/Translucency.md`'s T3). Over a theme whose background is the terminal's own there is
no colour to walk, so the panel keeps showing the desktop through, untouched -- imposing a
dithered edge by default on a theme that exists to be seen through is a theme author's call
to make with `ned/theme-surface`, not a default.

### Translucent overlays

Every wash the buffer paints over a line is composited over the buffer's own background
rather than replacing it, so any of them can carry alpha -- selection, search match and
snippet field, and (as of the text-layer pass) the merge-conflict ours/theirs/base tints,
`documentHighlight`, line-inspect, the DAP execution line, a multibuffer's added/removed
diff tints, and trailing whitespace:

```janet
(ned/theme-set "selection_background" "#61afef80")   # the accent at half strength
```

An opaque value behaves exactly as it always did -- it composites to itself, byte for byte,
which is why giving those fields an alpha channel changed nothing for any theme that had
already set them. Note the asymmetry with `selection_background`, which is the one field
that substitutes a default alpha when a theme leaves it fully opaque (every theme written
before the format had alpha says 255, and a solid bar over text is what that produces);
the rest stay exactly as authored. A detected theme uses this already: its
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
