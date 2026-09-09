//
// Phase 5 of Docs/Translucency.md: the chrome widgets paint through themed
// Surfaces now. The property worth pinning is not "gradients work" -- it is
// that an *unthemed* editor is byte-identical to what it painted before, so
// the migration is invisible until a theme asks for something.
//

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Mode.h"
#include "Editor/ThemeSetting.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/PluginLoader.h"
#include "JanetTestSupport.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"
#include "UI/ActiveBuffer.h"
#include "UI/ModeLine.h"
#include "UI/Paint.h"
#include "UI/PaintParse.h"
#include "UI/Theme.h"
#include "UI/ThemePaints.h"
#include "UI/Widget.h"

using ned::ui::Canvas;
using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::Screen;
using ned::ui::Surface;
using ned::ui::Theme;

namespace {

struct SurfaceGuard {
    SurfaceGuard() {
        ned::ui::ClearSurfaceOverrides();
        ned::ui::ClearNamedPaints();
    }
    ~SurfaceGuard() {
        ned::ui::ClearSurfaceOverrides();
        ned::ui::ClearNamedPaints();
    }
};

Screen PaintModeLine(const Theme& theme, int width) {
    static ned::text::Buffer buffer("chrome.txt", ned::text::Rope("hello"));
    ned::ui::ActiveBuffer    active(buffer);
    ned::editor::Mode        mode;
    ned::ui::ModeLine        modeLine(active, mode, theme);

    Screen screen(width, 1);
    modeLine.SetBox_(ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 0});
    Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 0});
    modeLine.Paint(canvas);
    return screen;
}

ned::ui::Paint ParseOrDie(std::string_view spec, const Theme& theme) {
    const auto paint = ned::ui::ParsePaint(spec, ned::ui::PaintContextFor(theme));
    REQUIRE(paint.has_value());
    return *paint;
}

} // namespace

TEST_CASE("An unthemed mode line paints the same gradient it always did", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme  = DarkTheme();
    Screen             screen = PaintModeLine(theme, 40);

    // Endpoints are exact in both the old hand-rolled interpolation and the
    // Surface path; the interior differs only in rounding.
    REQUIRE(screen.PixelAt(0, 0).background_color == theme.modeLineGradientStart);
    REQUIRE(screen.PixelAt(39, 0).background_color == theme.modeLineGradientEnd);
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.modeLineForeground);

    SECTION("and the interior is still a monotone ramp between them") {
        const int left  = screen.PixelAt(5, 0).background_color.red;
        const int mid   = screen.PixelAt(20, 0).background_color.red;
        const int right = screen.PixelAt(35, 0).background_color.red;
        const int from  = theme.modeLineGradientStart.red;
        const int to    = theme.modeLineGradientEnd.red;
        if (from <= to) {
            REQUIRE(left <= mid);
            REQUIRE(mid <= right);
        }
        else {
            REQUIRE(left >= mid);
            REQUIRE(mid >= right);
        }
    }
}

TEST_CASE("A themed mode line paints the theme's own paint", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    modeline.fill    = ParseOrDie("x #000000 #ffffff", theme);
    ned::ui::SetSurfaceOverride("modeline", modeline);

    Screen screen = PaintModeLine(theme, 40);
    REQUIRE(screen.PixelAt(0, 0).background_color == Color::RGB(0x000000));
    REQUIRE(screen.PixelAt(39, 0).background_color == Color::RGB(0xffffff));
    REQUIRE(static_cast<int>(screen.PixelAt(20, 0).background_color.red) == 131);

    SECTION("the row's text survives the fill -- a wash never erases glyphs") {
        std::string row;
        for (int x = 0; x < 40; ++x) {
            row += screen.PixelAt(x, 0).character;
        }
        REQUIRE(row.find("chrome.txt") != std::string::npos);
    }
}

TEST_CASE("A translucent mode line keeps the terminal's background", "[ChromeSurface]") {
    const SurfaceGuard guard;
    Theme              theme = DarkTheme();
    theme.background         = Color::Default;

    Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    modeline.fill    = ParseOrDie("x #40c08080 #40c08080", theme);
    ned::ui::SetSurfaceOverride("modeline", modeline);

    Screen screen = PaintModeLine(theme, 40);

    // Over a transparent background there is nothing to blend into, so the
    // wash reaches the desktop the only way a cell can: dithered coverage in
    // the blank cells, and a foreground tint where the row has text.
    bool anyDithered = false;
    for (int x = 0; x < 40; ++x) {
        const auto& cell = screen.PixelAt(x, 0);
        REQUIRE(cell.background_color == Color::Default);
        anyDithered = anyDithered || (cell.character != " " && cell.character != "");
    }
    REQUIRE(anyDithered);
}

TEST_CASE("A mode-line text fade modulates the glyphs after they are painted", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    modeline.fill    = ParseOrDie("x #000000 #000000", theme);
    modeline.text    = ParseOrDie("x 100% 0%", theme);
    ned::ui::SetSurfaceOverride("modeline", modeline);

    Screen screen = PaintModeLine(theme, 40);

    // Left edge keeps the whole foreground; the right edge has faded all the
    // way into the background it sits on.
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.modeLineForeground);
    REQUIRE(screen.PixelAt(39, 0).foreground_color == Color::RGB(0x000000));
}

namespace {

// A derived surface part is either a one-stop solid of the theme colour it
// came from, or -- when that colour is the terminal's own background, which
// has no RGB to paint with -- a paint that paints nothing at all.
void RequireDerivedFrom(const ned::ui::Paint& paint, const Color& colour) {
    if (colour.Composable()) {
        REQUIRE(paint.stops.size() == 1);
        REQUIRE(paint.stops.front().colour == colour);
    }
    else {
        REQUIRE(paint.stops.empty());
        REQUIRE_FALSE(ned::ui::PaintsColour(paint));
    }
}

} // namespace

TEST_CASE("Surface defaults keep every chrome widget's pre-migration colours", "[ChromeSurface]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "tab").fill, theme.tabBar.background);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "tab").text, theme.tabBar.foreground);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "tab.active").fill, theme.activeTab.background);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "tab.active.focused").fill, theme.modeLineFocusedGradientStart);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "panel").fill, theme.background);
    RequireDerivedFrom(ned::ui::SurfaceFor(theme, "panel").text, theme.defaultForeground);

    SECTION("a theme whose background is the terminal's own paints no panel fill") {
        // DarkTheme is exactly this case: the sidebar shows the terminal's
        // background through, which is what makes a transparent theme work
        // at all.
        REQUIRE_FALSE(theme.background.Composable());
        REQUIRE(ned::ui::SurfaceFor(theme, "panel").fill.stops.empty());
    }
}

TEST_CASE("A surface spec referencing a bundled preset applies the way startup applies it", "[ChromeSurface]") {
    // Mirrors main.cpp's own application order exactly: bundled presets are
    // registered first, then a surface spec that references one by name.
    // This is the path a live run takes, and it is worth pinning here
    // because a live run is the only other place it is exercised.
    const SurfaceGuard       guard;
    const Theme              theme = DarkTheme();
    ned::janet::Environment& env   = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);
    ned::editor::ClearNamedPaintOverrides();
    ned::editor::ClearSurfacePaintOverrides();
    ned::janet::LoadBundledPlugins(env);
    env.DoString(R"((ned/surface "modeline" :fill [:x "$spectrum"]))");

    for (const auto& [name, spec] : ned::editor::NamedPaintOverrides()) {
        const auto paint = ned::ui::ParsePaint(spec, ned::ui::PaintContextFor(theme));
        INFO(name << " = " << spec);
        REQUIRE(paint.has_value());
        ned::ui::RegisterNamedPaint(name, *paint);
    }

    const auto surfaces = ned::editor::SurfacePaintOverrides();
    REQUIRE(surfaces.size() == 1);
    REQUIRE(surfaces[0].spec == "x $spectrum");

    const auto paint = ned::ui::ParsePaint(surfaces[0].spec, ned::ui::PaintContextFor(theme));
    REQUIRE(paint.has_value());
    REQUIRE(paint->stops.size() == 4); // the preset's own stops, expanded
    REQUIRE(ned::ui::PaintsColour(*paint));

    Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    modeline.fill    = *paint;
    ned::ui::SetSurfaceOverride("modeline", modeline);

    Screen screen = PaintModeLine(theme, 40);
    REQUIRE(screen.PixelAt(0, 0).background_color == Color::RGB(0x2859dc));
    REQUIRE(screen.PixelAt(39, 0).background_color == Color::RGB(0xe13c6e));

    ned::editor::ClearNamedPaintOverrides();
    ned::editor::ClearSurfacePaintOverrides();
}
