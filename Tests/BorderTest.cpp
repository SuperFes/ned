#include <catch2/catch_test_macros.hpp>

#include <string>

#include "UI/Border.h"
#include "UI/Theme.h"
#include "UI/Widget.h"

namespace {

ned::ui::Canvas CanvasFor(ned::ui::Screen& screen) {
    return ned::ui::Canvas(screen,
                           ned::ui::Box{.x_min = 0, .x_max = screen.Width() - 1, .y_min = 0, .y_max = screen.Height() - 1});
}

} // namespace

TEST_CASE("DrawBorder paints rounded corners and line edges", "[Border]") {
    ned::ui::Screen screen(6, 4);
    ned::ui::Canvas canvas = CanvasFor(screen);
    const ned::ui::Brush brush{.foreground = ned::ui::Color::BrightBlack};

    ned::ui::DrawBorder(canvas, brush);

    REQUIRE(screen.PixelAt(0, 0).character == "╭");
    REQUIRE(screen.PixelAt(5, 0).character == "╮");
    REQUIRE(screen.PixelAt(0, 3).character == "╰");
    REQUIRE(screen.PixelAt(5, 3).character == "╯");
    REQUIRE(screen.PixelAt(2, 0).character == "─");
    REQUIRE(screen.PixelAt(2, 3).character == "─");
    REQUIRE(screen.PixelAt(0, 1).character == "│");
    REQUIRE(screen.PixelAt(5, 2).character == "│");
    // Interior untouched.
    REQUIRE(screen.PixelAt(2, 1).character == " ");
    // Brush applied to border cells.
    REQUIRE(screen.PixelAt(0, 0).foreground_color == brush.foreground);
}

TEST_CASE("DrawBorder on a single row or column degrades to a plain line", "[Border]") {
    ned::ui::Brush brush{};

    ned::ui::Screen row(4, 1);
    ned::ui::Canvas rowCanvas = CanvasFor(row);
    ned::ui::DrawBorder(rowCanvas, brush);
    REQUIRE(row.PixelAt(0, 0).character == "─");
    REQUIRE(row.PixelAt(3, 0).character == "─");

    ned::ui::Screen column(1, 3);
    ned::ui::Canvas columnCanvas = CanvasFor(column);
    ned::ui::DrawBorder(columnCanvas, brush);
    REQUIRE(column.PixelAt(0, 0).character == "│");
    REQUIRE(column.PixelAt(0, 2).character == "│");

    ned::ui::Screen single(1, 1);
    ned::ui::Canvas singleCanvas = CanvasFor(single);
    ned::ui::DrawBorder(singleCanvas, brush); // must not crash
    REQUIRE(single.PixelAt(0, 0).character == "│");
}

TEST_CASE("DrawBorderTitle embeds a padded title into the top edge", "[Border]") {
    ned::ui::Screen screen(12, 3);
    ned::ui::Canvas canvas = CanvasFor(screen);
    const ned::ui::Brush border{.foreground = ned::ui::Color::BrightBlack};
    const ned::ui::Brush title{.foreground = ned::ui::Color::BrightMagenta, .bold = true};

    ned::ui::DrawBorder(canvas, border);
    ned::ui::DrawBorderTitle(canvas, "NED", title);

    // ╭─ NED ─...─╮
    REQUIRE(screen.PixelAt(0, 0).character == "╭");
    REQUIRE(screen.PixelAt(1, 0).character == "─");
    REQUIRE(screen.PixelAt(2, 0).character == " ");
    REQUIRE(screen.PixelAt(3, 0).character == "N");
    REQUIRE(screen.PixelAt(4, 0).character == "E");
    REQUIRE(screen.PixelAt(5, 0).character == "D");
    REQUIRE(screen.PixelAt(6, 0).character == " ");
    REQUIRE(screen.PixelAt(7, 0).character == "─");
    REQUIRE(screen.PixelAt(11, 0).character == "╮");
    REQUIRE(screen.PixelAt(3, 0).foreground_color == title.foreground);
    REQUIRE(screen.PixelAt(3, 0).bold);
}

TEST_CASE("DrawBorderTitle truncates rather than overwriting the top-right corner", "[Border]") {
    ned::ui::Screen screen(8, 3);
    ned::ui::Canvas canvas = CanvasFor(screen);
    const ned::ui::Brush border{};
    const ned::ui::Brush title{};

    ned::ui::DrawBorder(canvas, border);
    ned::ui::DrawBorderTitle(canvas, "LongProjectName", title);

    // Width 8: text columns are 2..5 (max 4), leaving ─╮ at 6..7 intact.
    REQUIRE(screen.PixelAt(2, 0).character == " ");
    REQUIRE(screen.PixelAt(3, 0).character == "L");
    REQUIRE(screen.PixelAt(5, 0).character == "n");
    REQUIRE(screen.PixelAt(6, 0).character == "─");
    REQUIRE(screen.PixelAt(7, 0).character == "╮");
}

TEST_CASE("DrawBorderTitle on a too-narrow canvas is a no-op", "[Border]") {
    ned::ui::Screen screen(4, 2);
    ned::ui::Canvas canvas = CanvasFor(screen);
    const ned::ui::Brush border{};

    ned::ui::DrawBorder(canvas, border);
    ned::ui::DrawBorderTitle(canvas, "X", border); // width - 4 == 0 -> nothing to draw
    REQUIRE(screen.PixelAt(1, 0).character == "─");
    REQUIRE(screen.PixelAt(2, 0).character == "─");
}
