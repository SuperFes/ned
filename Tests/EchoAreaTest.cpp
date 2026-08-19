#include <catch2/catch_test_macros.hpp>

#include <string>

#include "UI/EchoArea.h"
#include "UI/Theme.h"

namespace {

std::string RowText(ned::ui::Screen& screen, int row, int width) {
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

    ned::ui::Screen screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    echoArea.Paint(canvas);
    REQUIRE(RowText(screen, 0, 5) == "hello");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.echoArea.foreground);
    REQUIRE(screen.PixelAt(0, 0).background_color == theme.echoArea.background);

    message = "updated";
    echoArea.Paint(canvas); // Paint reads message_ fresh each call, no external sync needed
    REQUIRE(RowText(screen, 0, 7) == "updated");
}

// fuzzy-candidate-list-styling follow-up.

TEST_CASE("EmphasizeForEchoArea/DimForEchoArea wrap text in invisible sentinel bytes, not visible markup",
          "[EchoArea]") {
    const std::string emphasized = ned::ui::EmphasizeForEchoArea("foo");
    REQUIRE(emphasized.find("foo") != std::string::npos);
    REQUIRE(emphasized.size() == 5); // "foo" plus one start and one end sentinel byte, both invisible

    const std::string dimmed = ned::ui::DimForEchoArea("bar");
    REQUIRE(dimmed.find("bar") != std::string::npos);
    REQUIRE(dimmed.size() == 5);
}

TEST_CASE("EchoArea::Paint renders an emphasized span bold and strips its sentinel bytes", "[EchoArea]") {
    const std::string message = "before " + ned::ui::EmphasizeForEchoArea("BOLD") + " after";
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ned::ui::Screen screen(30, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 0});
    echoArea.Paint(canvas);

    // The sentinel bytes are consumed as zero-width markup, not rendered as
    // their own columns -- "before BOLD after" reads contiguously, with no
    // gap where the invisible markers were.
    REQUIRE(RowText(screen, 0, 17) == "before BOLD after");
    REQUIRE_FALSE(screen.PixelAt(0, 0).bold);  // 'b' of "before" -- unaffected
    REQUIRE(screen.PixelAt(7, 0).bold);        // 'B' of "BOLD"
    REQUIRE(screen.PixelAt(10, 0).bold);       // 'D' of "BOLD"
    REQUIRE_FALSE(screen.PixelAt(12, 0).bold); // 'a' of "after" -- back to normal
}

TEST_CASE("EchoArea::Paint renders a dimmed span with a blended foreground and strips its sentinel bytes",
          "[EchoArea]") {
    const std::string message = "before " + ned::ui::DimForEchoArea("DIM") + " after";
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ned::ui::Screen screen(30, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 0});
    echoArea.Paint(canvas);

    REQUIRE(RowText(screen, 0, 16) == "before DIM after");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.echoArea.foreground);  // 'b' -- untouched
    REQUIRE(screen.PixelAt(7, 0).foreground_color != theme.echoArea.foreground);  // 'D' of "DIM" -- blended
    REQUIRE(screen.PixelAt(11, 0).foreground_color == theme.echoArea.foreground); // 'a' of "after" -- back to normal
}
