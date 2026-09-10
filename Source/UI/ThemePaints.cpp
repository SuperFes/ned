#include "ThemePaints.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <mutex>
#include "Compositing.h"

namespace ned::ui {

Color ChromeBackdrop(const Theme& theme);
Color SelectionFill(const Theme& theme);

namespace {

    // 43%: measured against the built-in themes' own selection colours as
    // the point where the text underneath stays fully readable while the run
    // of selected cells still reads as one block.
    constexpr std::uint8_t kDefaultSelectionAlpha = 110;

    // A mode line's trailing colour and a stand-in tab strip both want to be
    // present but not solid.
    constexpr std::uint8_t kTranslucentChromeAlpha = 150;

    // ~16%. The current line is the one wash that is up the whole time you
    // are typing, so it sits below every other overlay's strength -- a
    // selection or a search hit has to win against it, not tie.
    constexpr std::uint8_t kCurrentLineAlpha = 40;

    // The left dock's edge falloff, in percent of the way toward white. See
    // the "panel" branch of DerivedSurface for why this exists and why it
    // runs on the panel's own side.
    constexpr double       kPanelEdgeLift     = 4.0;
    constexpr std::uint8_t kModeLineFadeAlpha = 165;

    std::mutex& Lock() {
        static std::mutex mutex;
        return mutex;
    }

    std::map<std::string, Paint, std::less<>>& NamedPaints() {
        static std::map<std::string, Paint, std::less<>> paints;
        return paints;
    }

    std::optional<Color>& DetectedAccentStorage() {
        static std::optional<Color> accent;
        return accent;
    }

    std::optional<Color>& AssumedBackgroundStorage() {
        static std::optional<Color> background;
        return background;
    }

    std::map<std::string, Surface, std::less<>>& Surfaces() {
        static std::map<std::string, Surface, std::less<>> surfaces;
        return surfaces;
    }

    // The fixed slot table. Small on purpose: a gradient wants the handful
    // of colours that define a theme's character, not all ~70 fields (those
    // stay reachable through ned/theme-set by their own key).
    std::optional<Color> SlotOf(const Theme& theme, std::string_view slot) {
        if (slot == "bg") {
            return theme.background;
        }
        if (slot == "fg") {
            return theme.defaultForeground;
        }
        if (slot == "subtle") {
            return theme.commentForeground;
        }
        if (slot == "selection") {
            return theme.selectionBackground;
        }
        if (slot == "search") {
            return theme.isearchMatchBackground;
        }
        if (slot == "accent") {
            return theme.borderAccent.foreground;
        }
        if (slot == "desktop-accent") {
            // The machine's own accent, not the theme's -- falls back to the
            // theme's when nothing was detected, so the slot always resolves.
            if (const std::optional<Color> detected = DetectedAccent()) {
                return *detected;
            }
            return theme.borderAccent.foreground;
        }
        if (slot == "border") {
            return theme.border.foreground;
        }
        if (slot == "chrome-bg") {
            return theme.modeLineGradientStart;
        }
        if (slot == "chrome-fg") {
            return theme.modeLineForeground;
        }
        if (slot == "error") {
            return theme.diagnosticError;
        }
        if (slot == "warning") {
            return theme.diagnosticWarning;
        }
        if (slot == "info") {
            return theme.diagnosticInformation;
        }
        if (slot == "hint") {
            return theme.diagnosticHint;
        }
        if (slot == "added") {
            return theme.diffAddedBackground;
        }
        if (slot == "removed") {
            return theme.diffRemovedBackground;
        }
        if (slot == "comment") {
            return theme.commentForeground;
        }
        if (slot == "string") {
            return theme.stringForeground;
        }
        if (slot == "keyword") {
            return theme.keywordForeground;
        }
        if (slot == "number") {
            return theme.numberForeground;
        }
        if (slot == "type") {
            return theme.typeForeground;
        }
        if (slot == "function") {
            return theme.functionForeground;
        }
        return std::nullopt;
    }

    Paint SolidOrNothing(const Color& colour) {
        // Color::Default means "the terminal's own background", which is a
        // paint that paints nothing rather than a colour to composite.
        return colour.Composable() ? SolidPaint(colour) : Paint{};
    }

    // A colour moved `percent` of the way *away* from itself, keeping its own
    // hue -- PaintParse's own AdjustLightness rule, restated here because
    // that one is private to the parser and this is a derived default rather
    // than something parsed from a spec.
    //
    // Unlike AdjustLightness the direction is chosen rather than given, by
    // the same luminance test EnsureContrast uses: toward white from a dark
    // colour, toward black from a light one. A fixed "toward white" reads as
    // elevation on a dark theme and as nothing at all on a light one, where
    // the background already sits a couple of levels off white and has no
    // headroom left -- measured on gruvbox-light, which moved by 1.
    //
    // A colour with no RGB to move (the terminal's own background) comes
    // back unchanged, which is what makes the panel falloff a no-op on a
    // transparent theme.
    Color Lifted(const Color& colour, double percent) {
        if (!colour.Composable()) {
            return colour;
        }
        const Color target = RelativeLuminance(colour) > 0.5 ? Color::RGB(0x000000) : Color::RGB(0xFFFFFF);
        const auto  amount = static_cast<std::uint8_t>(std::lround(std::clamp(percent, 0.0, 100.0) * 2.55));
        return BlendOver(colour, target.WithAlpha(amount)).WithAlpha(colour.alpha);
    }

    // Lifted at the panel's outer (left) edge, settling to the buffer's own
    // background at its inner one. Both stops are equal on a transparent
    // theme, so this degrades to a paint that paints nothing.
    Paint PanelEdgeFill(const Color& background) {
        if (!background.Composable()) {
            return Paint{};
        }
        return GradientPaint(PaintAxis::X, {ColorStop{.colour = Lifted(background, kPanelEdgeLift)},
                                            ColorStop{.colour = background}});
    }

    Surface FromBrush(const Brush& brush) {
        Surface surface;
        surface.fill = SolidOrNothing(brush.background);
        surface.text = SolidOrNothing(brush.foreground);
        return surface;
    }

    // The right-hand end of a mode-line gradient. A theme carrying its own
    // alpha wins; a fully opaque end is treated as unspecified for the same
    // reason SelectionFill does -- every theme predating alpha says 255 by
    // default, and a bar that stops dead at the edge is what that produces.
    //
    // A fade needs something to fade *into*, though. With no backdrop at all
    // -- a transparent theme on a terminal whose own colour was never
    // detected -- a translucent end would dither into braille speckle
    // instead of blending, so leave it alone and keep the flat bar.
    Color FadedEnd(const Theme& theme, const Color& end) {
        if (!end.Composable() || !end.Opaque() || !ChromeBackdrop(theme).Composable()) {
            return end;
        }
        return end.WithAlpha(kModeLineFadeAlpha);
    }

    Surface DerivedSurface(const Theme& theme, std::string_view name) {
        Surface surface;

        if (name == "buffer") {
            surface.fill = SolidOrNothing(theme.background);
            surface.text = SolidOrNothing(theme.defaultForeground);
            return surface;
        }
        if (name == "buffer.current_line") {
            // The desktop's own accent, taken right down: enough tint to
            // read as "you are here" without the row looking like a band
            // drawn over the code. It lands on the backing layer, behind
            // the glyphs, so the line keeps every syntax colour it had.
            //
            // A theme that wants something louder sets the surface itself,
            // and should set fill alone -- a current-line marker is a
            // background, and the rest of the line's colour is the theme's
            // own business.
            const Color accent = DetectedAccent().value_or(theme.modeLineFocusedGradientStart);
            surface.fill       = SolidPaint(accent.WithAlpha(kCurrentLineAlpha));
            return surface;
        }
        if (name == "buffer.selection") {
            // SelectionFill, not the raw field: an opaque selectionBackground
            // is softened to a tint (see SelectionFill's own comment), and
            // the derived default has to be what the buffer actually paints
            // or the gallery shows a swatch nothing matches.
            surface.fill = SolidOrNothing(SelectionFill(theme));
            return surface;
        }
        if (name == "buffer.search") {
            surface.fill = SolidOrNothing(theme.isearchMatchBackground);
            return surface;
        }
        if (name == "modeline") {
            surface.fill = GradientPaint(PaintAxis::X, {ColorStop{theme.modeLineGradientStart, 1.0F},
                                                        ColorStop{FadedEnd(theme, theme.modeLineGradientEnd), 1.0F}});
            surface.text = SolidOrNothing(theme.modeLineForeground);
            return surface;
        }
        if (name == "modeline.focused") {
            surface.fill = GradientPaint(PaintAxis::X, {ColorStop{theme.modeLineFocusedGradientStart, 1.0F},
                                                        ColorStop{FadedEnd(theme, theme.modeLineFocusedGradientEnd), 1.0F}});
            surface.text = SolidOrNothing(theme.modeLineForeground);
            return surface;
        }
        if (name == "tab.strip") {
            // The row behind the tabs. Normally the buffer's own background,
            // so the gaps between tabs read as the buffer showing through --
            // but a theme whose background *is* the terminal's has nothing
            // there to show, and the strip comes out as a hole rather than
            // as chrome. In that case take the mode line's own tone at part
            // strength, which is the other end of the same frame.
            surface.fill = theme.background.Composable()
                               ? SolidOrNothing(theme.background)
                               : SolidPaint(theme.modeLineGradientStart.WithAlpha(kTranslucentChromeAlpha));
            return surface;
        }
        if (name == "tab") {
            return FromBrush(theme.tabBar);
        }
        if (name == "tab.active") {
            return FromBrush(theme.activeTab);
        }
        if (name == "tab.active.focused") {
            // Tab-restyle behaviour, preserved as its own surface: the
            // active tab takes the focused mode line's accent so the top and
            // bottom edges of a focused pane light up as one system.
            Surface focused = FromBrush(theme.activeTab);
            focused.fill    = SolidOrNothing(theme.modeLineFocusedGradientStart);
            return focused;
        }
        if (name == "echo") {
            return FromBrush(theme.echoArea);
        }
        if (name == "scrollbar") {
            return FromBrush(theme.scrollBar);
        }
        if (name == "popup") {
            surface.fill   = SolidOrNothing(theme.background);
            surface.border = SolidOrNothing(theme.border.foreground);
            surface.text   = SolidOrNothing(theme.defaultForeground);
            return surface;
        }
        if (name == "panel") {
            // The one derived default that is deliberately *not* the flat
            // colour the widget used to paint (Docs/Translucency.md phase 5's
            // byte-identical rule): a left-docked panel sitting flat against
            // the buffer meets it as one slab edge-on to another, and the
            // only thing separating them is the border glyph.
            //
            // A horizontal walk instead -- a few percent lighter at the
            // panel's outer edge, settling to exactly the buffer background
            // where the two meet, so the seam itself has no step in it at all
            // and the panel reads as lifting away from the buffer rather than
            // butting against it. Runs on the *panel's* side because the
            // buffer's side carries code: a wash there would tint the text
            // (Compositing.h's T3), which is the one thing an edge treatment
            // must not do.
            //
            // Measured at 4% (~10 levels either way): visible as a direction,
            // not as a band, and it costs the panel's own text nothing --
            // the gallery reports the same contrast at both ends.
            // A transparent theme is unaffected for free, since
            // AdjustLightness declines a colour with no RGB to move, leaving
            // both stops equal to Color::Default and the fill painting
            // nothing.
            surface.fill   = PanelEdgeFill(theme.background);
            surface.border = SolidOrNothing(theme.border.foreground);
            surface.text   = SolidOrNothing(theme.defaultForeground);
            return surface;
        }
        return surface; // unknown name: paints nothing
    }

} // namespace

void RegisterNamedPaint(std::string name, Paint paint) {
    const std::lock_guard guard(Lock());
    NamedPaints()[std::move(name)] = std::move(paint);
}

std::optional<Paint> NamedPaint(std::string_view name) {
    const std::lock_guard guard(Lock());
    const auto            it = NamedPaints().find(name);
    if (it == NamedPaints().end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> NamedPaintNames() {
    const std::lock_guard    guard(Lock());
    std::vector<std::string> names;
    names.reserve(NamedPaints().size());
    for (const auto& [name, paint] : NamedPaints()) {
        names.push_back(name);
    }
    return names;
}

void ClearNamedPaints() {
    const std::lock_guard guard(Lock());
    NamedPaints().clear();
}

void SetSurfaceOverride(std::string name, Surface surface) {
    const std::lock_guard guard(Lock());
    Surfaces()[std::move(name)] = std::move(surface);
}

std::optional<Surface> SurfaceOverride(std::string_view name) {
    const std::lock_guard guard(Lock());
    const auto            it = Surfaces().find(name);
    if (it == Surfaces().end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> SurfaceOverrideNames() {
    const std::lock_guard    guard(Lock());
    std::vector<std::string> names;
    names.reserve(Surfaces().size());
    for (const auto& [name, surface] : Surfaces()) {
        names.push_back(name);
    }
    return names;
}

void ClearSurfaceOverrides() {
    const std::lock_guard guard(Lock());
    Surfaces().clear();
}

void SetDetectedAccent(std::optional<Color> accent) {
    const std::lock_guard guard(Lock());
    DetectedAccentStorage() = accent;
}

std::optional<Color> DetectedAccent() {
    const std::lock_guard guard(Lock());
    return DetectedAccentStorage();
}

void SetAssumedBackground(std::optional<Color> background) {
    const std::lock_guard guard(Lock());
    AssumedBackgroundStorage() = background;
}

std::optional<Color> AssumedBackground() {
    const std::lock_guard guard(Lock());
    return AssumedBackgroundStorage();
}

PaintContext PaintContextFor(const Theme& theme) {
    PaintContext context;
    context.slot  = [&theme](std::string_view slot) { return SlotOf(theme, slot); };
    context.named = [](std::string_view name) { return NamedPaint(name); };
    return context;
}

Surface SurfaceFor(const Theme& theme, std::string_view name) {
    if (const std::optional<Surface> override = SurfaceOverride(name)) {
        return *override;
    }
    return DerivedSurface(theme, name);
}

// 43%: measured against the built-in themes' own selection colours as the
// point where the text underneath stays fully readable while the run of
// selected cells still reads as one block.

Color ChromeBackdrop(const Theme& theme) {
    if (theme.background.Composable()) {
        return theme.background;
    }
    if (const std::optional<Color> assumed = AssumedBackground()) {
        return *assumed;
    }
    return theme.background;
}

Color OverlayBackground(const Theme& theme, const Color& overlay) {
    if (theme.background.Composable()) {
        return BlendOver(theme.background, overlay);
    }
    // A transparent theme has nothing in the cell to blend with, so a
    // translucent overlay would land opaque -- the exact slab it exists to
    // avoid. Composite against the detected backdrop instead when one is
    // known: the selected cells stop being see-through, everything else
    // stays as it was.
    if (const std::optional<Color> assumed = AssumedBackground()) {
        return BlendOver(*assumed, overlay);
    }
    return BlendOver(theme.background, overlay);
}

Color SelectionFill(const Theme& theme) {
    if (!theme.selectionBackground.Composable() || !theme.selectionBackground.Opaque()) {
        return theme.selectionBackground;
    }
    return theme.selectionBackground.WithAlpha(kDefaultSelectionAlpha);
}

Color StickyTone(const Theme& theme) {
    // ~18%, one figure for every theme. This used to carry a second, heavier
    // alpha for transparent themes, where the band could only be expressed
    // as coverage dithering and scattered dots read fainter than a flat tint
    // of the same strength. The band is a real composite on the backing
    // layer now, in both cases, so there is nothing left to compensate for.
    constexpr std::uint8_t kStickyAlpha = 46;

    const Color tone = theme.tabBar.background.Composable() ? theme.tabBar.background : theme.modeLineGradientStart;
    return tone.WithAlpha(kStickyAlpha);
}

Color StickyHighlight(const Theme& theme) {
    return OverlayBackground(theme, StickyTone(theme));
}

Color TextColourAt(const Surface& surface, const Canvas& canvas, Point local, const Color& fallback) {
    if (!PaintsColour(surface.text)) {
        return fallback;
    }
    const Size&  size   = canvas.size();
    const double u      = size.width > 1 ? static_cast<double>(local.x) / (size.width - 1) : 0.0;
    const double v      = size.height > 1 ? static_cast<double>(local.y) / (size.height - 1) : 0.0;
    const Point  origin = canvas.Origin();
    const Color  colour = PaintColourAt(surface.text, u, v, origin.x + local.x, origin.y + local.y);
    return colour.alpha == 0 ? fallback : colour;
}

void ApplyTextFade(Canvas& canvas, const Surface& surface) {
    if (surface.text.kind == PaintKind::Fade) {
        Fill(canvas, surface.text);
    }
}

void ClearCanvas(Canvas& canvas, const Color& beneath) {
    Cell blank;
    blank.background_color = beneath;
    for (int y = 0; y < canvas.size().height; ++y) {
        for (int x = 0; x < canvas.size().width; ++x) {
            canvas[{.x = x, .y = y}] = blank;
        }
    }
}

std::vector<std::string> SurfaceNames() {
    // Must list every name DerivedSurface answers to. The two are separate
    // hand-maintained lists and nothing but SurfaceNamesTest makes them
    // agree -- "tab.strip" was derived and painted by TabBar for a while
    // while being absent from here, which made it invisible to both
    // M-x theme-gallery and Docs/Themes.md despite working perfectly if you
    // already knew the name.
    return {"buffer", "buffer.current_line", "buffer.selection", "buffer.search",
            "modeline", "modeline.focused", "tab.strip", "tab", "tab.active",
            "tab.active.focused", "echo", "scrollbar", "panel",
            "popup"};
}

} // namespace ned::ui
