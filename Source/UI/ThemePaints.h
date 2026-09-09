//
// The theme side of the paint system: what `$bg` resolves to, where a named
// paint lives, and how a widget asks for a Surface by name.
//
// Two process-wide registries, both the mutex-guarded-static shape this
// codebase already uses for every editor-wide setting (Editor/TabWidth.h and
// friends), because both are written from Janet on any thread and read from
// the paint path on the main one:
//
//   named paints    ned/theme-gradient -- $brand, $glass, the bundled presets
//   surface overrides  ned/theme-surface -- "popup", "panel.sidebar", ...
//
// Surfaces are deliberately *additive* over Theme's existing flat colour
// fields rather than a replacement for them. Every default below is derived
// from those fields, so an unthemed surface is byte-identical to what the
// widget paints today, and widgets can migrate one at a time (Docs/
// Translucency.md phases 5-7) instead of in one flag day.
//

#ifndef NED_UI_THEMEPAINTS_H
#define NED_UI_THEMEPAINTS_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Paint.h"
#include "PaintParse.h"
#include "Theme.h"

namespace ned::ui {

// --- named paints --------------------------------------------------------

void                                   RegisterNamedPaint(std::string name, Paint paint);
[[nodiscard]] std::optional<Paint>     NamedPaint(std::string_view name);
[[nodiscard]] std::vector<std::string> NamedPaintNames();
void                                   ClearNamedPaints();

// --- surface overrides ---------------------------------------------------

void                                   SetSurfaceOverride(std::string name, Surface surface);
[[nodiscard]] std::optional<Surface>   SurfaceOverride(std::string_view name);
[[nodiscard]] std::vector<std::string> SurfaceOverrideNames();
void                                   ClearSurfaceOverrides();

// --- the detected desktop accent -----------------------------------------

// The accent colour the desktop (or terminal) reported at startup, kept
// alongside the theme rather than inside it: a theme is swappable and this
// is a fact about the machine, so `$desktop-accent` keeps meaning "the
// colour my desktop uses" even after switching to a bundled theme that has
// an accent of its own.
//
// Unset when nothing was detected, in which case the slot falls back to the
// active theme's own accent -- a paint referencing it still resolves.
void                               SetDetectedAccent(std::optional<Color> accent);
[[nodiscard]] std::optional<Color> DetectedAccent();

// --- resolution ----------------------------------------------------------

// A context that resolves $slot against this theme's own fields and $name
// against the named-paint registry.
[[nodiscard]] PaintContext PaintContextFor(const Theme& theme);

// The surface a widget should paint: an override if a theme set one, else
// the default derived from `theme`'s flat fields. An unknown name yields a
// surface that paints nothing, which is what an unmigrated widget wants.
[[nodiscard]] Surface SurfaceFor(const Theme& theme, std::string_view name);

// The colour a glyph should take from a surface's text paint at one local
// cell, or `fallback` when that paint contributes no colour of its own --
// it is empty, or it is a Fade, which modulates the glyphs *after* they are
// painted rather than colouring them.
[[nodiscard]] Color TextColourAt(const Surface& surface, const Canvas& canvas, Point local, const Color& fallback);

// Applies a surface's text paint when it is a Fade, and does nothing
// otherwise. Called after a widget's glyphs are down, since a fade's whole
// job is to take some of what is already there away.
void ApplyTextFade(Canvas& canvas, const Surface& surface);

// Clears a canvas to `beneath` before a surface goes down. Cells persist
// between frames (the Screen is only rebuilt on resize), so a paint that
// legitimately covers nothing -- a fully transparent fill, a pattern's off
// phase -- would otherwise show the previous frame through the gap.
//
// `beneath` should be what the widget's own surface sits on, normally the
// theme's buffer background: a *translucent* fill composites against it, so
// a mode line fading to 55% fades into the buffer. Clearing to
// Color::Default instead would leave nothing to composite with and push
// every translucent fill down the dither path even on an opaque theme,
// which is what the first version of this did.
void ClearCanvas(Canvas& canvas, const Color& beneath = Color::Default);

// Every surface name with a derived default -- the vocabulary a theme can
// override, and what M-x theme-gallery will enumerate.
[[nodiscard]] std::vector<std::string> SurfaceNames();

} // namespace ned::ui

#endif // NED_UI_THEMEPAINTS_H
