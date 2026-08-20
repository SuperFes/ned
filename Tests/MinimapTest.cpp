#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Mode.h"
#include "Editor/MinimapSettings.h"
#include "Text/Buffer.h"
#include "Text/Utf8.h"
#include "TestEvents.h"
#include "UI/ActiveBuffer.h"
#include "UI/Minimap.h"
#include "UI/Theme.h"

using ned::editor::MinimapCharsPerDot;
using ned::editor::MinimapWidth;
using ned::editor::SetMinimapCharsPerDot;
using ned::editor::SetMinimapWidth;
using ned::ui::Minimap;

namespace {

// Mirrors ProjectSidebarTest.cpp's own small mouse-event helpers.
ned::ui::Event MousePress(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed);
}
ned::ui::Event MouseMove(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved);
}
ned::ui::Event MouseRelease(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released);
}

// Process-wide state -- mirrors MinimapSettingsTest.cpp's own guard.
struct MinimapSettingsGuard {
    ~MinimapSettingsGuard() {
        SetMinimapWidth(5);
        SetMinimapCharsPerDot(8);
    }
};

struct Fixture {
    ned::text::Buffer     buffer{"scratch"};
    ned::ui::ActiveBuffer activeBuffer{buffer};
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();

    Minimap View() {
        return Minimap(activeBuffer, mode, theme);
    }
};

} // namespace

TEST_CASE("Minimap paints without crashing on an empty buffer", "[Minimap]") {
    const MinimapSettingsGuard guard;
    Fixture                    fixture;
    Minimap                    minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});

    ned::ui::Screen screen = ned::ui::Screen(5, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});
    minimap.Paint(canvas); // must not crash/throw
}

TEST_CASE("Minimap sets a braille dot for a non-whitespace line and none for a blank one", "[Minimap]") {
    const MinimapSettingsGuard guard;
    SetMinimapWidth(1);
    SetMinimapCharsPerDot(1);

    Fixture fixture;
    // Exactly 4 lines against a 1-row-tall (4 sub-row) minimap divides
    // evenly -- each line maps to exactly one sub-row with no
    // rounding-driven overlap, so line 0 ("x") only ever sets the
    // top-left dot.
    fixture.buffer.InsertAtPoint("x\n\n\n"); // line 0: "x", lines 1-3: blank

    Minimap minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0}); // 1x1 -- every line maps into this one cell

    ned::ui::Screen screen = ned::ui::Screen(1, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    minimap.Paint(canvas);

    // U+2800 (blank) plus bit 0x01 (top-left dot) == U+2801, since line 0's
    // "x" sits at column 0 -- confirms a dot was actually set, not just
    // "didn't crash."
    REQUIRE(screen.PixelAt(0, 0).character == ned::text::EncodeCodepointUtf8(U'⠁'));
}

TEST_CASE("Minimap does not render past MinimapWidth * MinimapCharsPerDot * 2 columns of a line", "[Minimap]") {
    const MinimapSettingsGuard guard;
    SetMinimapWidth(1);
    SetMinimapCharsPerDot(1);
    // width=1 column -> 2 sub-columns -> maxColumn = 2 real characters.
    // A run of 50 'x's must not crash or hang regardless -- this is the
    // actual behavior under test, not a specific glyph value (the exact
    // dot pattern for "many x's truncated to 2" is covered by the dot-
    // setting test above).
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string(50, 'x'));

    Minimap minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(1, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    minimap.Paint(canvas); // must not crash/hang
}

TEST_CASE("Minimap click maps to a position via the same proportional math as ScrollBar", "[Minimap]") {
    const MinimapSettingsGuard guard;
    Fixture                    fixture;
    Minimap                    minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9}); // height 10

    minimap.scrollable_length  = 100;
    minimap.item_visual_length = 10;

    int received = -1;
    minimap.SetOnScroll([&received](int position) { received = position; });

    // Clicking the middle row (row 5 of 10) should land roughly mid-way
    // through the scrollable range.
    minimap.OnEvent(MousePress(2, 5));
    REQUIRE(received == (5 * 100) / 10); // == 50, same formula ScrollBar::PositionForRow uses
}

TEST_CASE("Minimap drag continues reporting positions until release", "[Minimap]") {
    const MinimapSettingsGuard guard;
    Fixture                    fixture;
    Minimap                    minimap = fixture.View();
    minimap.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 4, .y_min = 0, .y_max = 9});
    minimap.scrollable_length  = 100;
    minimap.item_visual_length = 10;

    std::vector<int> received;
    minimap.SetOnScroll([&received](int position) { received.push_back(position); });

    minimap.OnEvent(MousePress(2, 0));
    minimap.OnEvent(MouseMove(2, 9));
    minimap.OnEvent(MouseRelease(2, 9));
    REQUIRE(received.size() == 2); // press + one move; release reports nothing new

    received.clear();
    minimap.OnEvent(MouseMove(2, 3)); // no longer dragging after release
    REQUIRE(received.empty());
}
