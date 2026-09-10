#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "Editor/ThemeSetting.h"
#include "UI/Overlay.h"
#include "UI/PaintParse.h"
#include "UI/Theme.h"
#include "UI/ThemePaints.h"
#include "UI/ThemeResolve.h"
#include "UI/Widget.h"

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::Screen;
using ned::ui::Theme;

// Translucency phase 7c: a shadow falls outside the overlay's own Box, and a
// Canvas clips to its box, so OverlayHost paints it. Elevation defaults to 0,
// which is what keeps the whole feature inert until a theme asks for it.

namespace {

struct SurfaceGuard {
    SurfaceGuard() {
        Clear();
    }
    ~SurfaceGuard() {
        Clear();
    }
    static void Clear() {
        ned::editor::ClearSurfacePaintOverrides();
        ned::ui::ClearSurfaceOverrides();
        ned::ui::ClearNamedPaints();
    }
};

// A widget that fills its own box with one flat colour, so the shadow around
// it is unambiguous.
class SolidWidget : public ned::ui::Widget {
  public:
    void Paint(Canvas c) override {
        for (int y = 0; y < c.size().height; ++y) {
            for (int x = 0; x < c.size().width; ++x) {
                c[{.x = x, .y = y}].background_color = Color::RGB(0x40, 0x40, 0x40);
            }
        }
    }
};

// A 20x10 screen with the overlay occupying (4,2)-(11,6), painted over a
// known light backdrop so any darkening is measurable.
Screen PaintWithOverlay(const Theme& theme, bool wireTheme = true) {
    Screen screen(20, 10);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            screen.PixelAt(x, y).background_color = Color::RGB(0xC0, 0xC0, 0xC0);
        }
    }

    static SolidWidget   widget;
    ned::ui::OverlayHost host;
    if (wireTheme) {
        host.SetTheme(&theme);
    }
    host.Add(widget, [](ned::ui::Size) {
        return Box{.x_min = 4, .x_max = 11, .y_min = 2, .y_max = 6};
    });
    host.Reflow(ned::ui::Size{20, 10});
    host.Show(widget);
    host.Paint(screen);
    return screen;
}

void SetShadow(const std::string& spec, int elevation) {
    ned::editor::AddSurfacePaint("popup", "shadow", spec);
    ned::editor::AddSurfacePaint("popup", "elevation", std::to_string(elevation));
    ned::ui::ApplyPaintOverrides(DarkTheme());
}

} // namespace

TEST_CASE("No elevation means no shadow at all", "[OverlayShadow]") {
    const SurfaceGuard guard;
    const Theme        theme = DarkTheme();

    // The derived default elevation is 0, so every cell outside the overlay
    // keeps the backdrop exactly. This is the whole safety property: nothing
    // changes for anyone who has not asked.
    Screen screen = PaintWithOverlay(theme);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (x >= 4 && x <= 11 && y >= 2 && y <= 6) {
                continue; // the overlay itself
            }
            INFO("cell (" << x << ", " << y << ")");
            REQUIRE(screen.PixelAt(x, y).background_color == Color::RGB(0xC0, 0xC0, 0xC0));
        }
    }
}

TEST_CASE("An elevated surface darkens the cells its shadow falls on", "[OverlayShadow]") {
    const SurfaceGuard guard;
    SetShadow("2 1 0 #000000a0", 1); // hard-edged, offset right 2 and down 1

    Screen screen = PaintWithOverlay(DarkTheme());

    // Right of the overlay, on a row it covers: inside the shifted box.
    REQUIRE(screen.PixelAt(12, 3).background_color.red < 0xC0);
    // Below it, under a column it covers.
    REQUIRE(screen.PixelAt(6, 7).background_color.red < 0xC0);
    // Left of it and above it: outside the shift, untouched.
    REQUIRE(screen.PixelAt(3, 3).background_color == Color::RGB(0xC0, 0xC0, 0xC0));
    REQUIRE(screen.PixelAt(6, 1).background_color == Color::RGB(0xC0, 0xC0, 0xC0));
}

TEST_CASE("A shadow never darkens the overlay's own cells", "[OverlayShadow]") {
    const SurfaceGuard guard;
    SetShadow("2 1 3 #000000ff", 2); // deliberately big enough to reach back over the box

    Screen screen = PaintWithOverlay(DarkTheme());

    for (int y = 2; y <= 6; ++y) {
        for (int x = 4; x <= 11; ++x) {
            INFO("cell (" << x << ", " << y << ")");
            REQUIRE(screen.PixelAt(x, y).background_color == Color::RGB(0x40, 0x40, 0x40));
        }
    }
}

TEST_CASE("Radius softens the shadow with distance", "[OverlayShadow]") {
    const SurfaceGuard guard;
    SetShadow("1 1 3 #000000ff", 1);

    Screen screen = PaintWithOverlay(DarkTheme());

    // Along a row the overlay covers, stepping away from the shifted box's
    // right edge (x = 12): each step is strictly lighter than the last.
    const int near   = screen.PixelAt(13, 4).background_color.red;
    const int middle = screen.PixelAt(14, 4).background_color.red;
    const int far    = screen.PixelAt(15, 4).background_color.red;
    REQUIRE(near < middle);
    REQUIRE(middle < far);
    REQUIRE(far < 0xC0); // still within the radius, so still darkened
}

TEST_CASE("A host with no theme paints no shadow, however the theme is set", "[OverlayShadow]") {
    const SurfaceGuard guard;
    SetShadow("2 1 0 #000000ff", 4);

    // Every headless test constructs a host without wiring a Theme; that must
    // stay a plain no-op rather than a crash or a stray wash.
    Screen screen = PaintWithOverlay(DarkTheme(), /*wireTheme=*/false);
    REQUIRE(screen.PixelAt(12, 3).background_color == Color::RGB(0xC0, 0xC0, 0xC0));
}

TEST_CASE("A malformed shadow or elevation spec is rejected, not half-applied", "[OverlayShadow]") {
    const SurfaceGuard guard;

    ned::editor::AddSurfacePaint("popup", "shadow", "2 1 #000000ff");      // three fields
    ned::editor::AddSurfacePaint("popup", "shadow", "2 1 2 not-a-colour"); // bad colour
    ned::editor::AddSurfacePaint("popup", "shadow", "2 1 -1 #000000ff");   // negative radius
    ned::editor::AddSurfacePaint("popup", "elevation", "high");            // not a number
    ned::editor::AddSurfacePaint("popup", "elevation", "-2");              // negative

    REQUIRE(ned::ui::ApplyPaintOverrides(DarkTheme()) == 5);
    REQUIRE(ned::ui::SurfaceFor(DarkTheme(), "popup").elevation == 0);
}
