#include <catch2/catch_test_macros.hpp>

#include <ftxui/screen/screen.hpp>
#include <string>

#include "UI/EchoArea.h"
#include "UI/Theme.h"

namespace {

std::string RowText(ftxui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

} // namespace

TEST_CASE("EchoArea displays whatever the referenced message currently holds", "[EchoArea]") {
    std::string       message = "hello";
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    echoArea.Paint(canvas);
    REQUIRE(RowText(screen, 0, 5) == "hello");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.echoArea.foreground.ToFtxui());
    REQUIRE(screen.PixelAt(0, 0).background_color == theme.echoArea.background.ToFtxui());

    message = "updated";
    echoArea.Paint(canvas); // Paint reads message_ fresh each call, no external sync needed
    REQUIRE(RowText(screen, 0, 7) == "updated");
}
