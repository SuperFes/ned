#include <catch2/catch_test_macros.hpp>

#include <ox/ox.hpp>

#include "UI/ScrollArrowButton.h"

TEST_CASE("ScrollArrowButton paints its symbol at column 0 with the enabled brush by default", "[ScrollArrowButton]") {
    const ox::Brush            brush{.foreground = ox::XColor::BrightBlack};
    const ox::Brush            disabledBrush{.foreground = ox::XColor::BrightBlue};
    ned::ui::ScrollArrowButton button(U'▲', brush, disabledBrush);
    button.size = {.width = 1, .height = 1};

    ox::ScreenBuffer screen({.width = 1, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 1, .height = 1}};
    button.paint(canvas);

    REQUIRE(screen[{.x = 0, .y = 0}].symbol == U'▲');
    REQUIRE(screen[{.x = 0, .y = 0}].brush == brush);
}

TEST_CASE("SetEnabled(false) switches to the disabled brush", "[ScrollArrowButton]") {
    const ox::Brush            brush{.foreground = ox::XColor::BrightBlack};
    const ox::Brush            disabledBrush{.foreground = ox::XColor::BrightBlue};
    ned::ui::ScrollArrowButton button(U'▼', brush, disabledBrush);
    button.size = {.width = 1, .height = 1};

    button.SetEnabled(false);

    ox::ScreenBuffer screen({.width = 1, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 1, .height = 1}};
    button.paint(canvas);

    REQUIRE(screen[{.x = 0, .y = 0}].brush == disabledBrush);

    button.SetEnabled(true);
    button.paint(canvas);
    REQUIRE(screen[{.x = 0, .y = 0}].brush == brush);
}

TEST_CASE("Left mouse_press invokes the registered callback while enabled", "[ScrollArrowButton]") {
    const ox::Brush            brush{};
    ned::ui::ScrollArrowButton button(U'▼', brush, brush);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(clicks == 1);
    button.mouse_release(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(clicks == 2);
    button.mouse_release(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
}

TEST_CASE("Left mouse_press does not invoke the callback while disabled", "[ScrollArrowButton]") {
    const ox::Brush            brush{};
    ned::ui::ScrollArrowButton button(U'▲', brush, brush);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });
    button.SetEnabled(false);

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(clicks == 0);
}

TEST_CASE("mouse_press with a non-Left button does not invoke the callback", "[ScrollArrowButton]") {
    const ox::Brush            brush{};
    ned::ui::ScrollArrowButton button(U'▲', brush, brush);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Right});
    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});

    REQUIRE(clicks == 0);
}

TEST_CASE("mouse_press with no callback registered is a safe no-op", "[ScrollArrowButton]") {
    const ox::Brush            brush{};
    ned::ui::ScrollArrowButton button(U'▲', brush, brush);

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // must not crash
}

TEST_CASE("A held Left press starts the repeat timer; mouse_release stops it", "[ScrollArrowButton]") {
    const ox::Brush            brush{};
    ned::ui::ScrollArrowButton button(U'▼', brush, brush);
    button.SetOnClick([] {});

    REQUIRE_FALSE(button.IsRepeating());

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(button.IsRepeating());

    button.mouse_release(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE_FALSE(button.IsRepeating());
}

TEST_CASE("mouse_leave stops the repeat timer -- there's no mouse-capture to rely on", "[ScrollArrowButton]") {
    const ox::Brush            brush{};
    ned::ui::ScrollArrowButton button(U'▲', brush, brush);
    button.SetOnClick([] {});

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(button.IsRepeating());

    button.mouse_leave(); // e.g. dragged off the button before releasing
    REQUIRE_FALSE(button.IsRepeating());
}

TEST_CASE("A disabled button does not start the repeat timer on press", "[ScrollArrowButton]") {
    const ox::Brush            brush{};
    ned::ui::ScrollArrowButton button(U'▲', brush, brush);
    button.SetOnClick([] {});
    button.SetEnabled(false);

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE_FALSE(button.IsRepeating());
}

TEST_CASE("timer() invokes the callback while enabled and stops itself if disabled mid-hold", "[ScrollArrowButton]") {
    const ox::Brush            brush{};
    ned::ui::ScrollArrowButton button(U'▼', brush, brush);

    int clicks = 0;
    button.SetOnClick([&clicks] { ++clicks; });

    button.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(clicks == 1);

    button.timer(); // simulates a repeat tick, without a real sleep
    REQUIRE(clicks == 2);
    REQUIRE(button.IsRepeating());

    button.SetEnabled(false); // e.g. the buffer scrolled to the point this direction is exhausted
    button.timer();
    REQUIRE(clicks == 2); // no further click
    REQUIRE_FALSE(button.IsRepeating());
}
