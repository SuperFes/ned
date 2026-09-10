#include "ThemeResolve.h"

#include <string>
#include <utility>

#include "Editor/ThemeSetting.h"
#include "PaintParse.h"
#include "ThemeFile.h"
#include "ThemePaints.h"
#include "ThemeRegistry.h"

namespace ned::ui {

namespace {

    void Append(std::string& message, const std::string& note) {
        message = message.empty() ? note : message + "; " + note;
    }

    // The base-theme precedence chain. Split out so ResolveConfiguredTheme
    // reads as the four pipeline steps rather than as one long lambda.
    //
    // Two steps used to sit above ned/set-theme and are both gone.
    //
    // A theme.txt cache written by `ned --detect-theme`, which queried the
    // terminal's own colours over OSC: that probe had to run before anything
    // read stdin, so it could never be part of an ordinary launch and needed
    // a cache file plus a separate invocation to be useful -- and a cache
    // goes stale. DesktopTheme() below has none of those problems.
    //
    // And a remembered "theme" variable in $XDG_STATE_HOME, written by the
    // select-theme picker, which outranked ned/set-theme outright. A theme
    // is an explicit setting, so an implicit pin quietly overriding the
    // explicit one is the wrong shape: `(ned/set-theme "nord")` in an
    // init.janet appeared to be ignored, with nothing on screen to say why.
    // The picker applies for the session now and says where to write it
    // down; Editor/Variables.h keeps its other keys (sidebar width, panel
    // visibility, minimap state), which really are UI state.
    Theme BaseTheme(std::string& message, bool ignoreConfiguredName) {
        const std::string preferred = ignoreConfiguredName ? std::string() : editor::PreferredThemeName();
        if (!preferred.empty()) {
            if (auto named = ThemeByName(preferred)) {
                return *std::move(named);
            }
            Append(message, "Unknown theme \"" + preferred + "\" (ned/set-theme)");
        }
        if (const auto desktop = DesktopTheme()) {
            return BuildDesktopTheme(*desktop);
        }
        return DarkTheme();
    }

    // What a translucent overlay composites against when the theme's own
    // background is the terminal's. The theme deliberately keeps
    // Color::Default there so the desktop shows through the buffer; a
    // selection still needs *something* to tint, or it lands as the solid
    // slab it exists to avoid.
    void RememberAssumedBackground(const Theme& theme) {
        if (theme.background.Composable()) {
            SetAssumedBackground(theme.background);
            return;
        }
        // Nothing to read here: the terminal's real background could only
        // be probed before the event loop starts reading stdin, which is a
        // thing ned no longer does at all. Polarity from the theme's own
        // foreground is enough -- light text means a dark backdrop -- and it
        // only decides what a *translucent overlay* composites against,
        // never what gets painted where nothing is.
        const int  luma      = (299 * theme.defaultForeground.red + 587 * theme.defaultForeground.green +
                                114 * theme.defaultForeground.blue) /
                               1000;
        const bool lightText = !theme.defaultForeground.Composable() || luma >= 128;
        SetAssumedBackground(lightText ? Color::RGB(0x14141c) : Color::RGB(0xf0f0ec));
    }

} // namespace

std::optional<DesktopThemeInfo> DesktopTheme() {
    static const std::optional<DesktopThemeInfo> probed = ProbeDesktopTheme();
    return probed;
}

int ApplyPaintOverrides(const Theme& theme) {
    ClearNamedPaints();
    ClearSurfaceOverrides();

    int rejected = 0;
    for (const auto& [name, spec] : editor::NamedPaintOverrides()) {
        if (const auto paint = ParsePaint(spec, PaintContextFor(theme))) {
            RegisterNamedPaint(name, *paint);
        }
        else {
            ++rejected;
        }
    }
    for (const auto& override : editor::SurfacePaintOverrides()) {
        const auto paint = ParsePaint(override.spec, PaintContextFor(theme));
        if (!paint) {
            ++rejected;
            continue;
        }
        Surface surface = SurfaceFor(theme, override.surface);
        if (override.part == "fill") {
            surface.fill = *paint;
        }
        else if (override.part == "border") {
            surface.border = *paint;
        }
        else if (override.part == "text") {
            surface.text = *paint;
        }
        else {
            ++rejected;
            continue;
        }
        SetSurfaceOverride(override.surface, surface);
    }
    return rejected;
}

ResolvedTheme ResolveConfiguredTheme(bool ignoreConfiguredName) {
    ResolvedTheme resolved;
    resolved.theme = BaseTheme(resolved.message, ignoreConfiguredName);

    // Remember the machine's own accent for the $desktop-accent paint slot,
    // whichever theme won above -- a named theme or a saved theme file
    // should not stop a paint asking for "the colour my desktop uses". A
    // failed probe simply leaves the slot falling back to the active
    // theme's own accent.
    if (const auto desktop = DesktopTheme()) {
        SetDetectedAccent(desktop->accent);
    }

    RememberAssumedBackground(resolved.theme);

    // init.janet's accumulated (ned/theme-set ...) overrides, applied on top
    // of whichever base won -- a handful of targeted tweaks over a named
    // theme, which is how a theme is written (Docs/Themes.md). Insertion
    // order, so a later call for the same key wins.
    // Unknown keys/tokens are counted and reported once rather than silently
    // dropped -- unlike a theme *file*'s forward-compatibility case, these
    // were typed by the user against this exact build, so a typo'd key is
    // worth a message.
    {
        int rejected = 0;
        for (const auto& [key, token] : editor::ThemeColorOverrides()) {
            if (!SetThemeColorByKey(resolved.theme, key, token)) {
                ++rejected;
            }
        }
        if (rejected > 0) {
            Append(resolved.message, std::to_string(rejected) + " unrecognized ned/theme-set key(s)/color(s) ignored");
        }
    }

    if (const int rejected = ApplyPaintOverrides(resolved.theme); rejected > 0) {
        Append(resolved.message,
               std::to_string(rejected) + " unparseable ned/theme-gradient or ned/theme-surface spec(s) ignored");
    }

    return resolved;
}

} // namespace ned::ui
