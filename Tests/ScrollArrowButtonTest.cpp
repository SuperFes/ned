#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/animation.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/screen.hpp>

#include "UI/ScrollArrowButton.h"

namespace {

using ned::ui::Brush;
using ned::ui::Canvas;
using ned::ui::ScrollArrowButton;

ftxui::Event MousePress(int x, int y, ftxui::Mouse::Button button = ftxui::Mouse::Left) {
    ftxui::Mouse mouse;
    mouse.button = button;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x      = x;
    mouse.y      = y;
    return ftxui::Event::Mouse("", mouse);
}

ftxui::Event MouseRelease(int x, int y, ftxui::Mouse::Button button = ftxui::Mouse::Left) {
    ftxui::Mouse mouse;
    mouse.button = button;
    mouse.motion = ftxui::Mouse::Released;
    mouse.x      = x;
    mouse.y      = y;
    return ftxui::Event::Mouse("", mouse);
}

// Every test uses a 1x1 widget positioned at the screen origin, so
// LocalMouseEvent's own bounds check (Widget.h) always hits for (0, 0).
void PlaceAtOrigin(ScrollArrowButton& button) {
    button.SetBox_(ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
}

} // namespace

TEST_CASE("ScrollArrowButton paints its symbol at column 0 with the enabled brush by default", "[ScrollArrowButton]") {
    const Brush       brush{.foreground = ned::ui::Color::BrightBlack};
    const Brush       disabledBrush{.foreground = ned::ui::Color::BrightBlue};
    ScrollArrowButton button(U'▲', brush, disabledBrush);

    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    Canvas        canvas(screen, ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    button.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 0).character == "▲");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == brush.foreground.ToFtxui());
}

TEST_CASE("SetEnabled(false) switches to the disabled brush", "[ScrollArrowButton]") {
    const Brush       brush{.foreground = ned::ui::Color::BrightBlack};
    const Brush       disabledBrush{.foreground = ned::ui::Color::BrightBlue};
    ScrollArrowButton button(U'▼', brush, disabledBrush);

    button.SetEnabled(false);

    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    Canvas        canvas(screen, ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    button.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 0).foreground_color == disabledBrush.foreground.ToFtxui());

    button.SetEnabled(true);
    button.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).foreground_color == brush.foreground.ToFtxui());
}

TEST_CASE("Left mouse press invokes the registered callback while enabled", "[ScrollArrowButton]") {
    const Brush        brush{};
    ScrollArrowButton button(U'▼', brush, brush);
    PlaceAtOrigin(button);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });

    button.OnEvent(MousePress(0, 0));
    REQUIRE(clicks == 1);
    button.OnEvent(MouseRelease(0, 0));

    button.OnEvent(MousePress(0, 0));
    REQUIRE(clicks == 2);
    button.OnEvent(MouseRelease(0, 0));
}

TEST_CASE("Left mouse press does not invoke the callback while disabled", "[ScrollArrowButton]") {
    const Brush        brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });
    button.SetEnabled(false);

    button.OnEvent(MousePress(0, 0));
    REQUIRE(clicks == 0);
}

TEST_CASE("mouse press with a non-Left button does not invoke the callback", "[ScrollArrowButton]") {
    const Brush        brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });

    button.OnEvent(MousePress(0, 0, ftxui::Mouse::Right));
    button.OnEvent(MousePress(0, 0, ftxui::Mouse::WheelDown));

    REQUIRE(clicks == 0);
}

TEST_CASE("mouse press with no callback registered is a safe no-op", "[ScrollArrowButton]") {
    const Brush        brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);

    button.OnEvent(MousePress(0, 0)); // must not crash
}

TEST_CASE("A held Left press starts repeating; mouse release stops it", "[ScrollArrowButton]") {
    const Brush        brush{};
    ScrollArrowButton button(U'▼', brush, brush);
    PlaceAtOrigin(button);
    button.SetOnClick([] {});

    REQUIRE_FALSE(button.IsRepeating());

    button.OnEvent(MousePress(0, 0));
    REQUIRE(button.IsRepeating());

    button.OnEvent(MouseRelease(0, 0));
    REQUIRE_FALSE(button.IsRepeating());
}

TEST_CASE("A release anywhere stops the repeat -- there's no mouse-capture to rely on", "[ScrollArrowButton]") {
    // FTXUI delivers every mouse event to every leaf widget regardless of
    // position (see Widget.h's own header comment) -- unlike the old
    // TermOx-backed version, which needed a dedicated mouse_leave override
    // for exactly this drag-off-the-button-before-releasing scenario, a
    // release anywhere already reaches this widget's OnEvent.
    const Brush        brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);
    button.SetOnClick([] {});

    button.OnEvent(MousePress(0, 0));
    REQUIRE(button.IsRepeating());

    button.OnEvent(MouseRelease(50, 50)); // released far away from the button
    REQUIRE_FALSE(button.IsRepeating());
}

TEST_CASE("A disabled button does not start repeating on press", "[ScrollArrowButton]") {
    const Brush        brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);
    button.SetOnClick([] {});
    button.SetEnabled(false);

    button.OnEvent(MousePress(0, 0));
    REQUIRE_FALSE(button.IsRepeating());
}

TEST_CASE("OnAnimation invokes the callback once the interval elapses, and stops itself if disabled mid-hold",
          "[ScrollArrowButton]") {
    const Brush        brush{};
    ScrollArrowButton button(U'▼', brush, brush);
    PlaceAtOrigin(button);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });

    button.OnEvent(MousePress(0, 0));
    REQUIRE(clicks == 1);

    // Simulates a repeat tick (one animation step past the repeat interval)
    // without a real sleep.
    ftxui::animation::Params tick(ftxui::animation::Duration{0.15F});
    button.OnAnimation(tick);
    REQUIRE(clicks == 2);
    REQUIRE(button.IsRepeating());

    button.SetEnabled(false); // e.g. the buffer scrolled to the point this direction is exhausted
    ftxui::animation::Params tick2(ftxui::animation::Duration{0.15F});
    button.OnAnimation(tick2);
    REQUIRE(clicks == 2); // no further click
    REQUIRE_FALSE(button.IsRepeating());
}
