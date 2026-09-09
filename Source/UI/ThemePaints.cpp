#include "ThemePaints.h"

#include <map>
#include <mutex>

namespace ned::ui {

namespace {

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

    Surface FromBrush(const Brush& brush) {
        Surface surface;
        surface.fill = SolidOrNothing(brush.background);
        surface.text = SolidOrNothing(brush.foreground);
        return surface;
    }

    Surface DerivedSurface(const Theme& theme, std::string_view name) {
        Surface surface;

        if (name == "buffer") {
            surface.fill = SolidOrNothing(theme.background);
            surface.text = SolidOrNothing(theme.defaultForeground);
            return surface;
        }
        if (name == "buffer.current_line") {
            // Deliberately empty: ned has never highlighted the current
            // line's row (only its gutter number), so the default keeps that
            // exactly. A theme opts in, and when it does it should set fill
            // alone -- a current-line marker is a background, and the rest
            // of the line's colour is the theme's own business.
            return surface;
        }
        if (name == "buffer.selection") {
            surface.fill = SolidOrNothing(theme.selectionBackground);
            return surface;
        }
        if (name == "buffer.search") {
            surface.fill = SolidOrNothing(theme.isearchMatchBackground);
            return surface;
        }
        if (name == "modeline") {
            surface.fill = GradientPaint(PaintAxis::X, {ColorStop{theme.modeLineGradientStart, 1.0F},
                                                        ColorStop{theme.modeLineGradientEnd, 1.0F}});
            surface.text = SolidOrNothing(theme.modeLineForeground);
            return surface;
        }
        if (name == "modeline.focused") {
            surface.fill = GradientPaint(PaintAxis::X, {ColorStop{theme.modeLineFocusedGradientStart, 1.0F},
                                                        ColorStop{theme.modeLineFocusedGradientEnd, 1.0F}});
            surface.text = SolidOrNothing(theme.modeLineForeground);
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
        if (name == "panel" || name == "popup") {
            surface.fill   = SolidOrNothing(theme.background);
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
    return {"buffer", "buffer.current_line", "buffer.selection", "buffer.search",
            "modeline", "modeline.focused", "tab", "tab.active",
            "tab.active.focused", "echo", "scrollbar", "panel",
            "popup"};
}

} // namespace ned::ui
