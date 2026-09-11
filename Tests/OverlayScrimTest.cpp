#include <catch2/catch_test_macros.hpp>

#include "Editor/ThemeSetting.h"
#include "UI/Overlay.h"
#include "UI/PaintParse.h"
#include "UI/Theme.h"
#include "UI/ThemePaints.h"
#include "UI/ThemeRegistry.h"
#include "UI/ThemeResolve.h"
#include "UI/Widget.h"

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::Screen;
using ned::ui::Theme;
using ned::ui::ThemeByName;

// Translucency phase 5: the focus scrim. Everything a *focused* overlay does
// not cover is washed with the "scrim" surface, so the thing holding the
// keyboard reads as the thing holding the keyboard. The focus gate is the
// load-bearing part: a completion popup is visible while you type into the
// buffer behind it, and dimming what you are typing would be backwards.

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

class FocusableWidget : public ned::ui::Widget {
  public:
    [[nodiscard]] bool Focusable() const override {
        return true;
    }
    void Paint(Canvas c) override {
        for (int y = 0; y < c.size().height; ++y) {
            for (int x = 0; x < c.size().width; ++x) {
                c[{.x = x, .y = y}].background_color = Color::RGB(0x40, 0x40, 0x40);
            }
        }
    }
};

constexpr Box kOverlayBox{.x_min = 4, .x_max = 11, .y_min = 2, .y_max = 6};

// An opaque theme on purpose: the scrim derives from ChromeBackdrop, which on
// a transparent theme resolves to the *detected desktop* colour, and no
// desktop has been detected inside a test.
Theme OpaqueTheme() {
    return ThemeByName("gruvbox-light").value();
}

// A 20x10 screen of known backdrop with the overlay at kOverlayBox. `focused`
// decides whether the overlay takes keyboard focus before painting.
Screen PaintWithOverlay(const Theme& theme, bool focused) {
    Screen screen(20, 10);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            screen.PixelAt(x, y).background_color = Color::RGB(0xC0, 0xC0, 0xC0);
        }
    }

    FocusableWidget      widget;
    ned::ui::OverlayHost host;
    host.SetTheme(&theme);
    host.Add(widget, [](ned::ui::Size) { return kOverlayBox; });
    host.Reflow(ned::ui::Size{20, 10});
    host.Show(widget);
    if (focused) {
        widget.TakeFocus();
    }
    host.Paint(screen);
    return screen;
}

bool InOverlay(int x, int y) {
    return kOverlayBox.Contain(x, y);
}

} // namespace

TEST_CASE("A focused overlay scrims everything it does not cover", "[OverlayScrim]") {
    const SurfaceGuard guard;

    Screen screen = PaintWithOverlay(OpaqueTheme(), /*focused=*/true);

    const Color backdrop = Color::RGB(0xC0, 0xC0, 0xC0);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            INFO("cell (" << x << ", " << y << ")");
            if (InOverlay(x, y)) {
                // The overlay paints itself after the scrim, so its own cells
                // are untouched by it -- that is the point of exempting the
                // box rather than relying on overpainting.
                REQUIRE(screen.PixelAt(x, y).background_color == Color::RGB(0x40, 0x40, 0x40));
            }
            else {
                REQUIRE(screen.PixelAt(x, y).background_color != backdrop);
            }
        }
    }
}

TEST_CASE("A visible but unfocused overlay scrims nothing", "[OverlayScrim]") {
    const SurfaceGuard guard;

    // The completion-popup case: up, painted, and not what the keyboard is
    // talking to.
    Screen screen = PaintWithOverlay(OpaqueTheme(), /*focused=*/false);

    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (InOverlay(x, y)) {
                continue;
            }
            INFO("cell (" << x << ", " << y << ")");
            REQUIRE(screen.PixelAt(x, y).background_color == Color::RGB(0xC0, 0xC0, 0xC0));
        }
    }
}

TEST_CASE("A theme can silence the focus scrim outright", "[OverlayScrim]") {
    const SurfaceGuard guard;

    ned::editor::AddSurfacePaint("scrim", "fill", "#00000000");
    ned::ui::ApplyPaintOverrides(OpaqueTheme());

    Screen screen = PaintWithOverlay(OpaqueTheme(), /*focused=*/true);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (InOverlay(x, y)) {
                continue;
            }
            INFO("cell (" << x << ", " << y << ")");
            REQUIRE(screen.PixelAt(x, y).background_color == Color::RGB(0xC0, 0xC0, 0xC0));
        }
    }
}

TEST_CASE("The scrim dims a glyph's foreground rather than covering it", "[OverlayScrim]") {
    const SurfaceGuard guard;
    const Theme        theme = OpaqueTheme();

    Screen screen(20, 10);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            screen.PixelAt(x, y).background_color = Color::RGB(0xC0, 0xC0, 0xC0);
        }
    }
    // One cell carrying real text, well outside the overlay.
    screen.PixelAt(18, 9).character        = "M";
    screen.PixelAt(18, 9).foreground_color = Color::RGB(0x00, 0x00, 0x00);

    FocusableWidget      widget;
    ned::ui::OverlayHost host;
    host.SetTheme(&theme);
    host.Add(widget, [](ned::ui::Size) { return kOverlayBox; });
    host.Reflow(ned::ui::Size{20, 10});
    host.Show(widget);
    widget.TakeFocus();
    host.Paint(screen);

    // Still says "M" -- a scrim de-emphasises text, it does not erase it --
    // but no longer at full black. The foreground is the half that matters:
    // over an opaque theme the background wash composites the theme's own
    // colour onto itself and changes nothing, so a background-only scrim
    // would be invisible exactly where it is most needed.
    REQUIRE(screen.PixelAt(18, 9).character == "M");
    REQUIRE(screen.PixelAt(18, 9).foreground_color != Color::RGB(0x00, 0x00, 0x00));
}
