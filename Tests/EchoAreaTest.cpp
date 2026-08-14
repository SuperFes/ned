#include <catch2/catch_test_macros.hpp>

#include <ox/ox.hpp>
#include <string>

#include "UI/EchoArea.h"
#include "UI/Theme.h"

namespace {

std::u32string RowText(ox::ScreenBuffer& screen, int row, int width) {
    std::u32string out;
    for (int col = 0; col < width; ++col) {
        out.push_back(screen[{.x = col, .y = row}].symbol);
    }
    return out;
}

} // namespace

TEST_CASE("EchoArea displays whatever the referenced message currently holds", "[EchoArea]") {
    std::string       message = "hello";
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ox::ScreenBuffer screen({.width = 20, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 1}};

    echoArea.paint(canvas);
    REQUIRE(RowText(screen, 0, 5) == U"hello");
    REQUIRE(screen[{.x = 0, .y = 0}].brush == theme.echoArea);

    message = "updated";
    echoArea.paint(canvas); // paint reads message_ fresh each call, no external sync needed
    REQUIRE(RowText(screen, 0, 7) == U"updated");
}
