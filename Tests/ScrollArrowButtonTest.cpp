#include <catch2/catch_test_macros.hpp>

#include "TestEvents.h"
#include "UI/ScrollArrowButton.h"

namespace {

using ned::ui::Brush;
using ned::ui::Canvas;
using ned::ui::ScrollArrowButton;

ned::ui::Event MousePress(int x, int y, ned::ui::MouseEvent::Button button = ned::ui::MouseEvent::Button::Left) {
    return ned::ui::test::Mouse(x, y, button, ned::ui::MouseEvent::Motion::Pressed);
}

ned::ui::Event MouseRelease(int x, int y, ned::ui::MouseEvent::Button button = ned::ui::MouseEvent::Button::Left) {
    return ned::ui::test::Mouse(x, y, button, ned::ui::MouseEvent::Motion::Released);
}

// Every test uses a 1x1 widget positioned at the screen origin, so
// LocalMouseEvent's own bounds check (Widget.h) always hits for (0, 0).
void PlaceAtOrigin(ScrollArrowButton& button) {
    button.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
}

} // namespace

TEST_CASE("ScrollArrowButton paints its symbol at column 0 with the enabled brush by default", "[ScrollArrowButton]") {
    const Brush       brush{.foreground = ned::ui::Color::BrightBlack};
    const Brush       disabledBrush{.foreground = ned::ui::Color::BrightBlue};
    ScrollArrowButton button(U'▲', brush, disabledBrush);

    ned::ui::Screen screen = ned::ui::Screen(1, 1);
    Canvas          canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    button.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 0).character == "▲");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == brush.foreground);
}

TEST_CASE("SetEnabled(false) switches to the disabled brush", "[ScrollArrowButton]") {
    const Brush       brush{.foreground = ned::ui::Color::BrightBlack};
    const Brush       disabledBrush{.foreground = ned::ui::Color::BrightBlue};
    ScrollArrowButton button(U'▼', brush, disabledBrush);

    button.SetEnabled(false);

    ned::ui::Screen screen = ned::ui::Screen(1, 1);
    Canvas          canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    button.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 0).foreground_color == disabledBrush.foreground);

    button.SetEnabled(true);
    button.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).foreground_color == brush.foreground);
}

TEST_CASE("Left mouse press invokes the registered callback while enabled", "[ScrollArrowButton]") {
    const Brush       brush{};
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
    const Brush       brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });
    button.SetEnabled(false);

    button.OnEvent(MousePress(0, 0));
    REQUIRE(clicks == 0);
}

TEST_CASE("mouse press with a non-Left button does not invoke the callback", "[ScrollArrowButton]") {
    const Brush       brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });

    button.OnEvent(MousePress(0, 0, ned::ui::MouseEvent::Button::Right));
    button.OnEvent(MousePress(0, 0, ned::ui::MouseEvent::Button::WheelDown));

    REQUIRE(clicks == 0);
}

TEST_CASE("mouse press with no callback registered is a safe no-op", "[ScrollArrowButton]") {
    const Brush       brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);

    button.OnEvent(MousePress(0, 0)); // must not crash
}

TEST_CASE("A held Left press starts repeating; mouse release stops it", "[ScrollArrowButton]") {
    const Brush       brush{};
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
    // Every mouse event is delivered to every leaf widget regardless of
    // position (see Widget.h's own header comment), so a release anywhere
    // already reaches this widget's OnEvent.
    const Brush       brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);
    button.SetOnClick([] {});

    button.OnEvent(MousePress(0, 0));
    REQUIRE(button.IsRepeating());

    button.OnEvent(MouseRelease(50, 50)); // released far away from the button
    REQUIRE_FALSE(button.IsRepeating());
}

TEST_CASE("A disabled button does not start repeating on press", "[ScrollArrowButton]") {
    const Brush       brush{};
    ScrollArrowButton button(U'▲', brush, brush);
    PlaceAtOrigin(button);
    button.SetOnClick([] {});
    button.SetEnabled(false);

    button.OnEvent(MousePress(0, 0));
    REQUIRE_FALSE(button.IsRepeating());
}

// Repeat is a real background std::jthread that Post()s onClick_ back onto a
// real EventLoop (see ScrollArrowButton.h's own header comment), which needs
// an actual Notcurses context (a real tty) to construct at all, not something
// a headless unit test can drive deterministically. What's left testable
// without one: SetEventLoop defaulting to nullptr makes a held press
// register as repeating (IsRepeating() true, matching a real press) without
// ever spawning a thread or firing a second click on its own -- covered by
// the press/release tests above already. The actual repeat-firing and
// disable-mid-hold behavior is exercised by manual pty smoke testing instead
// (see ROADMAP.md), the same way this codebase already treats other
// real-elapsed-time-dependent behavior it can't unit-test headlessly.
TEST_CASE("A held press with no EventLoop registered stays 'repeating' but never fires a second click on its own",
          "[ScrollArrowButton]") {
    const Brush       brush{};
    ScrollArrowButton button(U'▼', brush, brush);
    PlaceAtOrigin(button);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });

    button.OnEvent(MousePress(0, 0));
    REQUIRE(clicks == 1);
    REQUIRE(button.IsRepeating());
}
