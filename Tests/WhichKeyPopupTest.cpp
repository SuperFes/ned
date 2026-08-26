#include <catch2/catch_test_macros.hpp>

#include "UI/Theme.h"
#include "UI/WhichKeyHint.h"
#include "UI/WhichKeyPopup.h"

namespace {

// Marks every cell with a distinctive character/background before Paint()
// runs, standing in for whatever the pane underneath already painted on a
// prior frame -- a leftover marker cell after Paint() means WhichKeyPopup
// left a gap in its own interior instead of painting a solid box.
void MarkAsStale(ned::ui::Screen& screen) {
    for (int y = 0; y < screen.Height(); ++y) {
        for (int x = 0; x < screen.Width(); ++x) {
            ned::ui::Cell& cell   = screen.PixelAt(x, y);
            cell.character        = "Z";
            cell.background_color = ned::ui::Color::RGB(0x11, 0x22, 0x33);
        }
    }
}

} // namespace

TEST_CASE("WhichKeyPopup fills its entire interior with the theme background, leaving no stale cells", "[WhichKeyPopup]") {
    ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::WhichKeyPopup popup(theme);
    popup.SetHint(ned::ui::WhichKeyHint{.prefixLabel = "C-x-", .bindings = {{"o", "other-window"}}});

    ned::ui::Screen screen = ned::ui::Screen(30, 6);
    MarkAsStale(screen);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 5});
    popup.Paint(canvas);

    for (int y = 1; y < 5; ++y) {
        for (int x = 1; x < 29; ++x) {
            const ned::ui::Cell& cell = screen.PixelAt(x, y);
            INFO("cell (" << x << ", " << y << ")");
            REQUIRE(cell.character != "Z");
            REQUIRE(cell.background_color == theme.background);
        }
    }
}

TEST_CASE("WhichKeyPopup degrades sanely for a zero-area canvas", "[WhichKeyPopup]") {
    ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::WhichKeyPopup popup(theme);
    popup.SetHint(ned::ui::WhichKeyHint{.prefixLabel = "C-x-", .bindings = {}});

    ned::ui::Screen screen = ned::ui::Screen(0, 0);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = -1, .y_min = 0, .y_max = -1});
    popup.Paint(canvas); // must not crash
}
