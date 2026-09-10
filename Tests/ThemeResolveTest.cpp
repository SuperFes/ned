#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/ThemeSetting.h"
#include "UI/Paint.h"
#include "UI/Theme.h"
#include "UI/ThemePaints.h"
#include "UI/ThemeResolve.h"

using ned::ui::ApplyPaintOverrides;
using ned::ui::DarkTheme;
using ned::ui::LightTheme;
using ned::ui::Theme;

namespace {

// Both stores are process-wide (Editor/ThemeSetting.h's deferred specs and
// UI/ThemePaints.h's parsed registries), so this cleans up after itself the
// way BundledPaintsTest.cpp's own guard does.
struct PaintStoreGuard {
    PaintStoreGuard() : assumedBackground_(ned::ui::AssumedBackground()), detectedAccent_(ned::ui::DetectedAccent()) {
        Clear();
    }
    ~PaintStoreGuard() {
        Clear();
        // Not part of the paint registries, but process-wide all the same,
        // and ResolveConfiguredTheme writes both. Leaving AssumedBackground
        // set changes OverlayBackground's answer for every theme whose
        // background is the terminal's own -- which is DarkTheme's -- so a
        // leak here silently reshades the selection bar in every other
        // widget test that runs after this file.
        ned::ui::SetAssumedBackground(assumedBackground_);
        ned::ui::SetDetectedAccent(detectedAccent_);
    }
    static void Clear() {
        ned::editor::ClearNamedPaintOverrides();
        ned::editor::ClearSurfacePaintOverrides();
        ned::ui::ClearNamedPaints();
        ned::ui::ClearSurfaceOverrides();
    }

  private:
    std::optional<ned::ui::Color> assumedBackground_;
    std::optional<ned::ui::Color> detectedAccent_;
};

} // namespace

TEST_CASE("ApplyPaintOverrides resolves stored specs against the given theme", "[ThemeResolve]") {
    const PaintStoreGuard guard;

    ned::editor::AddNamedPaint("wall", "$bg");
    REQUIRE(ApplyPaintOverrides(DarkTheme()) == 0);

    const auto dark = ned::ui::NamedPaint("wall");
    REQUIRE(dark.has_value());
    REQUIRE_FALSE(dark->stops.empty());
    REQUIRE(dark->stops.front().colour == DarkTheme().background);
}

TEST_CASE("A $slot paint follows a theme swap rather than keeping the old theme's colour", "[ThemeResolve]") {
    const PaintStoreGuard guard;

    // The bug this exists to prevent: select-theme replaces the base theme
    // without touching the spec store, so a paint parsed at startup held a
    // colour from a theme that was no longer showing.
    ned::editor::AddNamedPaint("wall", "$bg");
    ApplyPaintOverrides(DarkTheme());
    ApplyPaintOverrides(LightTheme());

    const auto light = ned::ui::NamedPaint("wall");
    REQUIRE(light.has_value());
    REQUIRE(light->stops.front().colour == LightTheme().background);
    REQUIRE_FALSE(light->stops.front().colour == DarkTheme().background);
}

TEST_CASE("ApplyPaintOverrides starts from an empty registry each time", "[ThemeResolve]") {
    const PaintStoreGuard guard;

    ned::editor::AddNamedPaint("gone", "$bg");
    ApplyPaintOverrides(DarkTheme());
    REQUIRE(ned::ui::NamedPaint("gone").has_value());

    // Clearing the deferred store and re-applying has to actually remove a
    // paint that is no longer registered, not leave the previous run's copy
    // resident -- what a theme switch relies on.
    ned::editor::ClearNamedPaintOverrides();
    ApplyPaintOverrides(DarkTheme());
    REQUIRE_FALSE(ned::ui::NamedPaint("gone").has_value());
}

TEST_CASE("Surface parts are applied over the derived default, and a bad part is counted", "[ThemeResolve]") {
    const PaintStoreGuard guard;

    ned::editor::AddSurfacePaint("popup", "fill", "$bg");
    ned::editor::AddSurfacePaint("popup", "elevation", "$bg"); // not a part
    ned::editor::AddSurfacePaint("popup", "fill", "not a colour at all");

    REQUIRE(ApplyPaintOverrides(DarkTheme()) == 2);

    const auto popup = ned::ui::SurfaceOverride("popup");
    REQUIRE(popup.has_value());
    REQUIRE(popup->fill.stops.front().colour == DarkTheme().background);
}

TEST_CASE("A named paint can be referenced by a surface registered after it", "[ThemeResolve]") {
    const PaintStoreGuard guard;

    // Insertion order is load-bearing: named paints are applied first
    // precisely so a surface spec can name one.
    ned::editor::AddNamedPaint("brand", "x $accent $keyword");
    ned::editor::AddSurfacePaint("panel", "fill", "$brand");

    REQUIRE(ApplyPaintOverrides(DarkTheme()) == 0);

    const auto panel = ned::ui::SurfaceOverride("panel");
    REQUIRE(panel.has_value());
    REQUIRE(panel->fill.stops.size() == 2);
}

TEST_CASE("ResolveConfiguredTheme reports unrecognized colour overrides", "[ThemeResolve]") {
    const PaintStoreGuard guard;
    ned::editor::ClearThemeColorOverrides();

    ned::editor::AddThemeColorOverride("no_such_key_at_all", "#123456");
    const ned::ui::ResolvedTheme resolved = ned::ui::ResolveConfiguredTheme();
    REQUIRE(resolved.message.find("unrecognized") != std::string::npos);

    ned::editor::ClearThemeColorOverrides();
}
