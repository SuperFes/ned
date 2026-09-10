# Translucency, Gradients, and Theme Engine v2

Design for making ned's UI genuinely translucent -- over its own content and over the
desktop behind the terminal -- and for the theme-engine rework that has to happen first.

Ground-truthed against `Source/UI/Widget.h` (`Color`/`Cell`/`Screen`), `Source/UI/Theme.h`,
`Source/UI/ThemePalette.h`, `Source/UI/ThemeFile.h`, and the measurements in
`Tools/NotcursesGradientProbe.cpp` / `Tools/TerminalImageAlphaProbe.cpp` (both of which
exist to keep this document from being guesswork).

## The one architectural fact everything follows from

**ned already owns a compositor.** Every widget paints into one shared `Screen` of `Cell`s
in painter order, and `Screen::Flush` hands the finished grid to Notcurses once per frame.
So translucency inside ned is *our* blend, not Notcurses': give `Color` an alpha byte and
`Screen` a blending write path, and we get exact 8-bit alpha with no protocol, no terminal
support, and none of Notcurses' 2-bit cell-alpha quantization.

That quantization is real and measured (`NotcursesGradientProbe`, page 1): stacked
`NCALPHA_BLEND` planes can only express 0 / 50 / 75 / 87.5 / 94 / 97%. We never have to
touch it. `Color::Interpolate` + `ModeLine`'s existing gradient are the precedent -- this
design generalises what that one widget already does by hand.

The exceptions to "ned owns every pixel" are the two places that hand Notcurses a real
plane of its own: `Minimap`'s pixel blitter and the `MemoryImageView`. They sit outside
the `Screen` grid and outside this model.

## What the medium actually allows

Measured, not assumed:

- **Konsole 26.08 honours real per-pixel alpha from an iTerm2-protocol image, all the way
  out to the window backdrop** -- desktop windows show through the low-alpha end of a
  gradient. Notcurses cannot drive this: 3.0.17 declares `NCPIXEL_ITERM2` but never
  selects or implements it, so no `--termtype` and no environment variable reaches it.
  `Tools/TerminalImageAlphaProbe.cpp` speaks the protocol directly.
- **A cell is one glyph plus one foreground plus one background.** This is the constraint
  that shapes the whole design. A background wash and a transparent background are
  mutually exclusive, and a cell that must show a code glyph cannot also show a dither
  pattern. There is no layering *within* a cell.
- **`Color::Default` is the only background that reaches the desktop.** Everything else is
  a colour the terminal paints opaquely, however we arrived at it. ned already supports
  this end to end: the bundled `dark`/`light` themes keep `Color::Default` as their
  background for exactly this reason.

## The image path, measured to its end

Konsole honours a PNG's alpha channel per pixel, out to the window backdrop -- the staircase
renders as five distinct steps with the desktop visible through the low-alpha end. That much
was known. What was still assumed rather than tested was whether such an image could sit
*behind text*, which is what any highlight band needs.

It cannot. Drawing the band first and then writing text into those cells removes the band;
drawing the band after the text covers the text. **An image and a glyph are mutually
exclusive per cell**, so this path cannot back a current-line highlight, a sticky-header
band, or anything else with content on it.

Where it stays useful is regions that carry no text of their own -- the minimap (already
pixel-blitted), a gutter decoration, a purely graphical panel. For a row of code, an opaque
background on a plane beneath the text is the ceiling, which is what `Screen`'s backing
layer now provides.

Cost, for the record: one 40-cell band was 0.16 ms to encode and 27 KB on the wire, but that
payload is an artefact of the probe's stored-deflate encoder -- the same flat band through
zlib level 6 is 126 bytes rather than 51 KB. Bandwidth was never going to be the blocker;
the cell model was.

## Foreground alpha, measured

`Tools/NotcursesLayerProbe.cpp` page two varies the *foreground* alpha of a text plane over
a filled plane whose own foreground is yellow `#e6c83c`, asking white for the glyphs each
time. What Notcurses emitted:

| mode | emitted glyph colour | meaning |
|---|---|---|
| `NCALPHA_OPAQUE` | `#ffffff` | the requested colour, unchanged |
| `NCALPHA_BLEND` | `#f2e39d` | the exact average of white and the plane below's `#e6c83c` |
| `NCALPHA_TRANSPARENT` | `#e6c83c` | the plane below's own foreground; this plane's colour discarded |
| `NCALPHA_HIGHCONTRAST` over a light plane | `#3f3f3f` | computed dark -- neither the requested white nor the plane's own grey |
| `NCALPHA_HIGHCONTRAST` over a dark plane | `#ffffff` | computed light, same request, opposite answer |
| `NCALPHA_HIGHCONTRAST` over *nothing* | `#ffffff` | assumed a dark backdrop |

Two of these are usable tools rather than curiosities.

**`TRANSPARENT` foregrounds mean a plane can contribute a glyph while a lower plane
contributes its colour** -- the composition a background-layer-under-a-text-layer design
would want, if we ever move ned's own two-pass idea onto real Notcurses planes rather than
doing it inside our own compositor.

**`HIGHCONTRAST` asks Notcurses to pick a legible colour** instead of being told one, which
is exactly the shape of "mark this row without painting a background". The caveat is in the
last row: over the terminal's own background there is nothing to measure, so it *assumes*
dark and answers white. That is right for a dark terminal and wrong for a light one, and
Notcurses cannot tell which it is -- so it is a good tool for a known backdrop and a guess
otherwise.

Note that ned does not currently use Notcurses cell alpha at all: our own compositor
resolves everything and hands `Screen::Flush` opaque colours. Using `HIGHCONTRAST` would
mean letting specific cells carry an alpha mode through to Notcurses.

## The six techniques

Every idea below is built from these. They are not interchangeable -- each one answers a
different question about what is already in the cell.

| # | technique | works when | gives |
|---|---|---|---|
| T1 | **Alpha blend into `Screen`** | destination bg is a known colour | exact 8-bit alpha, gradients, washes |
| T2 | **Coverage dithering** (8x8-ordered braille, full block at 100%) | destination bg is `Default` *and* the cell has no glyph | translucency to the desktop, ~9 usable levels |
| T3 | **Foreground tinting** | destination bg is `Default` and the cell *has* a glyph | highlight without losing transparency or the glyph |
| T4 | **Sub-cell colour** (half blocks / quadrants) | chrome rows we own outright | 2x vertical gradient resolution |
| T5 | **In-app blur** | region beneath is known colours | real frosted glass: sample `Screen`, box-blur, use as fill |
| T6 | **iTerm2 image layer** | Konsole / iTerm2 only, and only where no text goes | smooth true alpha over the desktop |

T5 is the one worth dwelling on: because we own the `Screen`, a popup can read the cells it
is about to cover, blur their colours, and use that as its own fill. That is genuine frosted
glass, computed by us, with no terminal involvement at all.

## Decisions taken

1. **Truecolor is the baseline.** Themes are authored with real RGB and are responsible
   for their own contrast; Notcurses degrades at the edges where a terminal can't keep up,
   and we stop maintaining a parallel design for it.
2. **The ANSI fallback path goes.** The 16 named colour constants are defined in one place
   (`Widget.h`), so redefining them as TrueColor left every call site untouched; then
   `AnsiDarkTheme`/`AnsiLightTheme`/`AnsiFallbackFor`, their registry entries, the startup
   swap and the applier gate in `main.cpp`, and `EventLoop::CanTrueColor`/`PaletteSize`
   (dead once the swap went). Theme files are now truecolor in both directions -- nothing
   writes an `x:<n>` token, and reading one resolves it to real RGB so pre-existing files
   still load.

   **`Kind::Palette16` itself stays**, contrary to the first draft of this plan. It has one
   legitimate consumer that the audit turned up: `Editor/Terminal/Emulator.cpp` maps
   libvterm's indexed SGR colours onto it, and a program running inside the embedded
   terminal should keep rendering in the user's *own* palette rather than being rewritten
   to our idea of red. Nothing else can produce one, so no blend path has to define alpha
   over a palette index -- which was the actual goal.
3. **Desktop bleed-through is a first-class design goal**, not a per-theme curiosity. The
   buffer background is expected to be `Color::Default` in the fanciest themes, which means
   T2/T3 are load-bearing rather than fallbacks, and every surface must declare what it
   does when it lands on a transparent cell.
4. **Theme v2 is a Surface/Paint model**, not an alpha byte bolted onto today's flat
   struct.

## Compositing model

`Cell` keeps its shape; `Color` gains `alpha` (255 default, so every existing construction
is unchanged). `Screen` gains one new write path:

```
Screen::Blend(y, x, Cell src, AlphaPolicy policy)
```

Resolution rules, in order:

1. `src.background.alpha == 255` -> plain write, today's behaviour exactly.
2. destination background is a known colour -> **T1**: lerp, write the result opaque.
3. destination background is `Default` and destination has no glyph -> **T2** if the
   policy allows: pick a dither glyph for the coverage, foreground = the wash colour,
   background stays `Default`.
4. destination background is `Default` and destination has a glyph -> **T3**: leave the
   background alone, shift the *foreground* toward the wash colour by the alpha.
5. policy says `Opaque` -> write the wash colour opaque, losing transparency (the escape
   hatch for surfaces that must be legible above all).

A translucent *foreground* always recolours the glyph and never touches the background.

`AlphaPolicy` is per-`Paint`, not global, because the right answer genuinely differs:
a modal scrim wants T3-over-text (dim the code, keep the window see-through), a popup body
wants T2 (frost the empty space), a mode line wants T1 (it owns its row outright).

## Theme engine v2

### Types

```
Color        { kind: Default|TrueColor, r, g, b, alpha }      // Palette16 removed
Stop         { offset: float, colour: Color }
Paint        = Solid{Color}
             | Linear{Axis axis, vector<Stop>}                 // X | Y | Diag
             | Radial{Stop centre..edge}
             | Blur{radius, tint: Color}                       // T5
Shadow       { dx, dy, radius, colour }                        // alpha-carrying
Surface      { fill: Paint, border: Paint, shadow: Shadow, elevation, policy: AlphaPolicy }
```

`Theme` stops being ~70 flat colour fields and becomes a keyed registry of `Surface`s
(`"popup"`, `"panel.sidebar"`, `"modeline"`, `"tab.active"`, ...), with the hot syntax-class
path kept enum-indexed exactly as it is now for speed. `SyntaxTheme`'s two-tier
per-class/per-capture inheritance is untouched -- it is orthogonal and it works.

### Authoring and serialisation

- Token format extends to `#rrggbbaa`; alpha is serialised only when it isn't 255, so every
  existing `ned/theme-set` call keeps working byte for byte.
- `ned/theme-set` gains a table form beside its string form:
  `(ned/theme-set "modeline.fill" {:gradient [[0.0 "#1e1e2eff"] [1.0 "#313244cc"]] :axis :x})`
- `ned/theme-surface` sets a whole bundle:
  `(ned/theme-surface "popup" {:fill {...} :border "#00000000" :shadow {:dx 1 :dy 1 :alpha 0.35} :policy :dither})`
- `ThemePalette` still derives a complete theme from ~15 semantic slots; it gains derived
  *paints* (panel fill = base background at 85% with a subtle vertical ramp, popup = blur +
  tint, and so on) so cloning an upstream palette stays a 15-line job.

### Contrast guard

With translucency everywhere and no ANSI fallback to hide behind, contrast becomes the
theme author's whole job -- so the engine should help: a `minContrast` floor, checked after
compositing, nudging the foreground when a wash pushes a pair under threshold. Cheap, and it
stops every theme independently rediscovering that a 40% wash over syntax colours is
unreadable. `high-contrast-dark`/`-light` already hold a raised floor; this generalises the
same idea into the compositor.

## What we build with it

**Text layer -- tint instead of replace.** Selection, isearch matches, diff-gutter line
tints, DAP execution and breakpoint rows, merge hunks, and inactive preprocessor regions all
currently *replace* the background and destroy syntax highlighting. As washes (T1) or
foreground tints (T3) they layer instead. Merge conflicts get the overlap behaviour for
free: ours at 25%, theirs at 25%, and where they overlap the two composite into a third
colour with no special-casing.

**Current line.** A horizontal gradient strongest at the gutter and gone by the right edge,
plus a vertical falloff centred on point -- focus, not a flat bar fighting the text.

**Current line -- and a layering idea worth trying first.** The obvious implementation runs
straight into the cell constraint: over a transparent background a fill has nothing to
composite into where the line has text, so `AlphaPolicy::Auto` falls through to tinting the
foreground, which is exactly what a background-only highlight should not do. A suggested
alternative (2026-09-09) is to stop treating it as one pass: paint a *background layer*
first -- current line, selection, diff tints, whatever else is a wash -- and then paint the
text layer on top writing only foregrounds, leaving the background it lands on alone unless
a span genuinely overrides it.

**Done, and on real planes rather than a second `Screen`.** `Screen` carries a backing grid
flushed to an `ncplane` below the text one (`EventLoop::BackingPlane`, `Canvas::Backing`), and
a text cell with no background of its own defers to it instead of painting the terminal
default over it. A wash picks its layer per cell: nothing in the cell means the backing layer,
where the tint lands over whatever is showing through -- the desktop included; the theme's own
background already in the cell means compositing in place, since there is nothing to gain from
a lower plane when the upper one is solid; and any *other* background is something louder that
already owns the cell, which a wash leaves alone. The current-line highlight and both sticky
bands (buffer and sidebar) go through this. Note the layer is only written by whoever wants
something there, unlike the text layer every widget repaints in full, so the render loop clears
it each frame or a highlight trails the cursor.

**Recency glow.** `UnsavedChangeRanges` plus an `EventLoop` timer: freshly edited lines get a
wash that decays over a few seconds. Peripheral, cheap, and nothing else in the terminal
world does it.

**Mode line.** Gradients driven by state rather than decoration: LSP indexing, test-run
progress, async load progress as a fill sweeping the bar; VCS dirty ratio as a hue shift.
T4 doubles the vertical resolution of any chrome gradient.

**Tabs.** Active tab a vertical gradient fading into the buffer below it, no hard seam;
inactive tabs translucent so content ghosts through; modified state as wash intensity.

**Panels and slide-outs.** Sidebar, VCS panel, ACP panel, terminal drawer, debug console as
translucent surfaces with an edge falloff at the docking side. Plus a **scrim**: when a panel
takes focus, one global wash dims everything else -- a better focus indicator than a border
colour, and one line of compositor work.

**Popups.** Outer border fully transparent, body translucent or blurred (T5), shadow from an
alpha falloff. Over a transparent buffer the same surface reads as text floating on a lightly
dithered scrim above the desktop. Completion popup, hover docs, `TreeView`, `ListPopup`, and
LSP peek all inherit it from one `elevation` value instead of each hand-rolling a look.

**Minimap.** Float it translucently over the right edge of the text rather than spending a
column, with a translucent viewport slider. (Note its pixel plane sits outside the `Screen`
compositor -- see the known z-order interaction.)

**Virtual text.** Inline diagnostics, inline blame, fold placeholders at 30-50% alpha rather
than hand-picked "dim" colours.

## Phasing

Each phase is independently testable against the headless `Screen`, and nothing is visible
until phase 4.

1. `Color` gains alpha; `Screen::Blend` and the five resolution rules; unit tests over a
   headless `Screen`. No visual change -- everything still writes opaque.
2. `Paint`/`Stop`/`Shadow`/`Surface` types, `Canvas::Fill(box, Paint)`, `AlphaPolicy`, the
   dither and foreground-tint paths (T2/T3), contrast guard. Tests only.
3. ANSI-fallback removal (decision 2 above) -- deliberately before theme v2 lands, so the
   new engine never has to define alpha-over-palette-index semantics.
4. Theme v2: keyed surface registry, `#rrggbbaa`, table-form `ned/theme-set`,
   `ned/theme-surface`, `ThemePalette` derived paints, `Docs/Themes.md`
   rewrite.
5. Chrome adoption: mode line, tabs, sidebar, panels, scrim. Low risk, no text interaction.
6. Text-layer adoption: current line, selection, isearch, diff, merge, virtual text. Highest
   value, and the phase that must not damage readability -- the contrast guard earns itself
   here.
7. Popups: transparent outer border, translucent body, shadow, blur (T5), elevation presets.
8. Optional and separate: the T6 image layer, if we ever want photographic smoothness over
   the desktop. The probe stays the reference implementation.

## Authoring gradients without becoming a CSS parser

The theme surface is already a scripting language (Janet now, jank later), so a gradient
needs no text grammar at all -- it needs a *value* shape, and an array is the right one.
The entire spec is one rule:

> A paint is an optional axis keyword, then stop values, with optional numbers between them
> as relative weights.

Strings are stops. Numbers are weights. That is the whole grammar.

```janet
(ned/theme-set "modeline.fill"   "#1e1e2e")                        # one stop = solid
(ned/theme-set "modeline.fill"   [:x "#1e1e2e" "#313244"])         # even split
(ned/theme-set "tab.active.fill" [:y "#89b4faff" 3 "#89b4fa00"])   # 3:1 -- most of the tab
                                                                   # stays solid, quick fade
(ned/theme-set "popup.fill"      [:radial "$bg/70" 2 "$bg/30"])
```

**Weights rather than offsets**, deliberately. `[0.0 c] [0.6 c] [1.0 c]` forces the author
into a normalised coordinate space, renumbers every stop when one is inserted, and invites
the CSS hard-stop trick (two stops at the same offset) that we have no use for. Weights are
integers, compose by insertion, and lose nothing: any offset set is expressible as
consecutive weights (a stop at 35% is just `35 ... 65`). Omitted weights are 1, so an
unweighted array is an even split.

Axes are `:x`, `:y`, `:diag`, `:radial`, defaulting to `:y` -- most chrome gradients run with
the light, top to bottom. Radial centres on the box, because a centre parameter is the first
step toward the parser we are not writing.

The same array flattens to one line for the flat `key=value` theme file, and it is what
A theme's own authoring surface covers:

```
modeline.fill = x #1e1e2e #313244
tab.active.fill = y #89b4faff 3 #89b4fa00
```

Parsing that is: split on whitespace; leading token may be an axis; numeric tokens are
weights; everything else is a stop. No tokenizer, no precedence, no nesting. When the
scripting language changes, only the binding that turns a host array into `vector<Stop>`
changes -- `ParsePaint` and the string form are language-agnostic.

### The paint kinds, and why `Fade` matters

```
Solid    one stop
Gradient axis + colour stops             -- absolute colours
Fade     axis + percentage stops         -- modulates the colour already there
Pattern  pattern keyword + period + stops -- checkerboards and friends, below
Blur     radius + tint                   -- T5
Stack    several paints, painted in order
```

`Fade` is what makes foreground gradients usable. A gradient that *replaces* the foreground
is right for chrome and wrong for code -- it would fight syntax highlighting for every glyph
it touched. What code wants is the syntax colour it already has, faded:

```janet
(ned/theme-set "buffer.overflow.text" [:x "100%" "0%"])   # long lines fade off the right edge
(ned/theme-set "buffer.folded.text"   "45%")              # folded placeholder, dimmed
(ned/theme-set "scroll.edge.text"     [:y "100%" 4 "25%"]) # content fading under a sticky header
```

A percentage stop makes it a `Fade`; a colour stop makes it a `Gradient`. Same array shape,
same weights, no second syntax -- and mixing the two in one paint is simply an error.

So both background and foreground take any paint, and the convention is: chrome uses
`Gradient` freely on either, buffer text uses `Fade` on the foreground and `Gradient` on the
background only.

### Patterns

A pattern is the same array with a pattern keyword where the axis went, and it costs the
grammar exactly one clause:

> A pattern keyword may be followed by one number, its period in cells. Every other number
> is still a weight.

Weights keep their meaning too -- in a pattern they read as duty cycle, which is the same
idea (what proportion of the span each phase gets):

```janet
(ned/theme-set "gallery.swatch.fill" [:checker 1 "$bg/0" "$subtle/22"])
(ned/theme-set "panel.fill"          [:noise 8 "$bg"])
(ned/theme-set "buffer.readonly.fill" [:hatch 3 "$bg/0" "$subtle/30"])
(ned/theme-set "retro.panel.fill"    [:stripes 6 "$bg" 3 "$accent1/18"])   # 1:3 duty cycle
```

The bundled set, all deterministic pure functions of cell position:

```
:checker n     alternating n-cell squares
:stripes n     diagonal bands, period n
:scanlines     every other row (period 2 unless given)
:grid n        one-cell rules every n -- graph paper
:hatch n       diagonal single-cell hatching
:dots n        stipple, one dot every n cells
:noise n       deterministic per-cell jitter of +/- n%, hashed from (x, y)
```

`:noise` is hashed from the cell's own coordinates rather than a running RNG, so it does not
shimmer between frames -- static texture, not animation. It is also the one people will
reach for most, because a large flat fill looks dead in a terminal and 8% of grain fixes it.

**Patterns skip cells that carry a glyph** by default. A checkerboard behind code is a
readability disaster, so a pattern textures the empty space of a surface -- the part of a
panel below its last row, the padding around popup text, the gutter -- and leaves text cells
to whatever the surface's own fill says. A theme that genuinely wants a pattern behind text
can say so with the paint's `AlphaPolicy`.

**Stacking is what makes patterns tasteful.** Grain *over* a gradient, not instead of it:

```janet
(ned/theme-set "panel.fill" [:stack "$lift" [:noise 6 "$bg"]])
(ned/theme-set "popup.fill" [:stack "$glass" [:scanlines "$bg/0" "$bg-6"]])
```

`:stack` paints its members in order, each through the ordinary blend rules, so a
translucent pattern over a translucent gradient composites the way you would expect.

And over a transparent background the classic case comes out right for free: a checker of
`"$bg/0"` and `"$subtle/22"` resolves through T2 into a genuinely see-through
checkerboard -- which is exactly what the theme gallery wants behind its alpha swatches.

### Two multipliers that keep it short

**Palette references.** Most gradients are "the base colour, slightly lifted" -- writing that
as two hex literals is tedious and goes stale the moment the palette changes. So a stop may
name a `ThemePalette` slot with a small adjustment:

```
$bg          $bg+8         $accent1-12        $accent2/60
slot         lighten 8%    darken 12%         60% alpha
```

Three postfix operations, deliberately. Anything more expressive is the scripting language's
job -- it is a real language, and a theme that wants a computed colour can compute it.

**Named paints.** Define once, use everywhere:

```janet
(ned/theme-gradient "brand" [:diag "$accent1" 2 "$accent3"])
(ned/theme-set "popup.border" "$brand")
(ned/theme-set "tab.active.fill" "$brand")
```

### Bundled presets

Presets ship as a bundled Janet plugin (`Source/Janet/Plugins/gradients.janet`, embedded
the way `vcs-git.janet` already is) rather than as C++ constants, so they load before
`init.janet` and a user redefining one by name simply wins -- the existing bundled-plugin
precedent, and it makes every preset a readable example of the format.

They come in two families, and the split matters.

**Palette-derived -- safe in every theme, and what the derived surfaces use by default.**
These resolve against the active `ThemePalette`, so they follow a theme instead of fighting
it:

```janet
(ned/theme-gradient "lift"   [:y "$bg" "$bg+6"])                  # panel fill, barely there
(ned/theme-gradient "sink"   [:y "$bg-5" "$bg+3"])                # inset wells, input rows
(ned/theme-gradient "glass"  [:y "$bg/78" 3 "$bg/52"])            # popup body
(ned/theme-gradient "edge"   [:x "$bg/0" 6 "$bg/65"])             # dock-edge falloff
(ned/theme-gradient "scrim"  "$bg/55")                            # focus dimmer
(ned/theme-gradient "focus"  [:x "$accent1/32" 4 "$accent1/0"])   # current-line wash
(ned/theme-gradient "brand"  [:diag "$accent1" 2 "$accent3"])     # tabs, popup borders
(ned/theme-gradient "rule"   [:x "$subtle/0" "$subtle" "$subtle/0"])  # separator, fades at both ends
```

`rule` is the small one worth stealing: a hairline that fades out at both ends reads as a
separator without drawing a hard line across the UI.

**Signature -- absolute colours, deliberately loud, opt-in.** `spectrum` is the ramp from
`Tools/NotcursesGradientProbe.cpp`, kept byte for byte because it is a genuinely good
four-stop path through the hues:

```janet
(ned/theme-gradient "spectrum" [:x "#2859dc" "#28c8be" "#e6be3c" "#e13c6e"])
(ned/theme-gradient "aurora"   [:x "#0b3d54" "#17c3a2" 2 "#7c5cff"])
(ned/theme-gradient "sunset"   [:x "#ff8a3c" "#ff3c7d" 2 "#6a2c9c"])
(ned/theme-gradient "ember"    [:x "#2a0a0a" 2 "#b3341c" "#ffb238"])
(ned/theme-gradient "ice"      [:x "#0b2545" 2 "#4cc9f0" "#e0fbfc"])
(ned/theme-gradient "vapor"    [:x "#ff71ce" "#01cdfe" "#05ffa1"])
(ned/theme-gradient "ink"      [:y "#0d0d12" "#4a4a58"])
```

Plus two `Fade` presets for the text layer, where absolute colour is the wrong tool:

```janet
(ned/theme-gradient "vanish" [:x "100%" "0%"])   # trail off toward an edge
(ned/theme-gradient "ghost"  "45%")              # present but receded
```

**Pattern presets**, for the weirder end -- and for the several places a pattern is the
correct answer rather than a decoration:

```janet
(ned/theme-gradient "grain"     [:noise 8 "$bg"])                  # texture on flat panel fills
(ned/theme-gradient "scanlines" [:scanlines "$bg/0" "$bg-8"])      # CRT, for themes that want it
(ned/theme-gradient "checker"   [:checker 1 "$bg/0" "$subtle/22"]) # the alpha-swatch indicator
(ned/theme-gradient "hatch"     [:hatch 3 "$bg/0" "$subtle/30"])   # read-only, inactive, disabled
(ned/theme-gradient "graph"     [:grid 8 "$bg/0" "$subtle/18"])    # graph paper behind empty space
(ned/theme-gradient "stipple"   [:dots 3 "$bg/0" "$subtle/28"])
```

`hatch` earns its place beyond novelty: read-only buffers, clangd's inactive preprocessor
regions, and the base side of a merge all want to say "this is not ordinary editable text"
without stealing a colour, and diagonal hatching says it in a way no tint does.

**Composition falls out of the existing rule.** A stop token naming a paint expands to that
paint's stops, so presets are building blocks rather than terminal values -- no new syntax:

```janet
(ned/theme-set "modeline.fill"    [:x "$spectrum"])          # same ramp, re-axed
(ned/theme-set "tab.active.fill"  [:y "$brand" 3 "$bg/0"])   # brand fading into the buffer
(ned/theme-set "gallery.header"   [:diag "$spectrum" "$bg"]) # extended with a settling stop
```

**Where the loud ones actually belong.** A spectrum behind code is unreadable, and the
contrast guard will fight it -- so signature presets are for chrome that owns its own row or
box: a mode-line progress fill, the `theme-gallery` header, a tab strip in a theme that
wants flair, a popup border. `M-x theme-gallery` lists every registered paint, bundled ones
included, which is also how someone discovers them.

### The payoff for transparent themes

A background gradient carrying alpha, landing on `Color::Default` cells, resolves through T2
automatically -- the author writes an ordinary two-stop gradient and gets a dithered fade
into the desktop, with no extra syntax and nothing new to learn. The same spec means "blend"
over known colours and "coverage" over transparency; the compositor decides, not the theme.

And because `ThemePalette` derives paints rather than just colours, a cloned upstream theme
gets tasteful gradients for free without its author writing one.

### Where the grammar actually lives

One implementation decision worth stating, because it is what keeps the grammar portable:
**C++ parses exactly one form -- the one-line string** ("y $bg 3 $bg+8"). The array form is
sugar, and the sugar lives in the bundled `gradients.janet` plugin, which flattens an array
into that string and calls `ned/theme-gradient`. Three things fall out of that:

- The binding layer stays free of any UI dependency, matching the layering `ned/theme-set`
  already has -- Janet knows nothing about `Paint`, and `Editor/ThemeSetting.h` stores the
  spec as an uninterpreted string until `main.cpp` has a real `Theme` to resolve `$slot`
  references against.
- Nesting (`:stack`) is expressible in the array form and deliberately not in the string
  form, which is why a stack's layers are usually *named* paints -- exactly the composition
  the presets already encourage.
- When the scripting language changes, `gradients.janet` is rewritten in jank and the
  parser is not touched at all.

Surfaces are additive over `Theme`'s existing flat colour fields rather than a replacement
for them: `SurfaceFor(theme, name)` returns an override if a theme set one, else a default
derived from those same fields, so an unmigrated widget's surface is byte-identical to what
it paints today and widgets can move one at a time.

### The feedback loop is the real feature

Gradient authoring lives or dies on iteration speed, so the format matters less than these
two:

- **`M-x theme-gallery`** -- a buffer showing every surface as a labelled swatch, redrawn on
  reload. What the probe pages are for Notcurses, pointed at our own themes instead, and it
  doubles as the contrast guard's reporting surface.

Implementation cost for the whole section: `ParsePaint` over both array and string forms,
`PaintAt(paint, u, v)`, a named-paint registry, and palette-ref resolution -- call it 200
lines and a test file.

## Risks and open questions

- **Readability is the real risk**, not performance. Every wash over code costs contrast;
  phase 6 needs screenshots and a critical eye more than it needs tests.
- **Per-frame cost** is negligible for blending (a few ops per cell over a ~5k-cell grid) but
  real for blur; blur should be computed when a popup's covered region changes, not per frame.
- **Transparent themes lose T1 everywhere** -- with the buffer background `Default`, most
  in-buffer highlights become T3 foreground tints. Whether a foreground-only selection reads
  well enough to be the default in transparent themes is an open question that only a
  screenshot can answer.
- **Sub-cell techniques and the cursor.** T2/T4 put glyphs where the user expects to click and
  select; mouse hit-testing and selection maths must keep treating those cells as background.
