#include <catch2/catch_test_macros.hpp>

#include "TestEvents.h"
#include "UI/MemoryImageView.h"
#include "UI/Theme.h"

TEST_CASE("MemoryImageView fills its entire interior with the theme background, leaving no stale cells", "[MemoryImageView]") {
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    ned::ui::MemoryImageView view(theme);
    view.SetModel(ned::ui::MemoryImageModel{.title = "Memory image: 0x1000 (4 bytes)", .bytes = {0, 64, 128, 255}});

    ned::ui::Screen screen = ned::ui::Screen(30, 10);
    for (int y = 0; y < screen.Height(); ++y) {
        for (int x = 0; x < screen.Width(); ++x) {
            ned::ui::Cell& cell   = screen.PixelAt(x, y);
            cell.character        = "Z";
            cell.background_color = ned::ui::Color::RGB(0x11, 0x22, 0x33);
        }
    }
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 9});
    view.Paint(canvas);

    for (int y = 1; y < 9; ++y) {
        for (int x = 1; x < 29; ++x) {
            const ned::ui::Cell& cell = screen.PixelAt(x, y);
            INFO("cell (" << x << ", " << y << ")");
            REQUIRE(cell.character != "Z");
        }
    }
}

TEST_CASE("MemoryImageView degrades sanely for a zero-area canvas and an empty model", "[MemoryImageView]") {
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    ned::ui::MemoryImageView view(theme);
    view.SetModel(ned::ui::MemoryImageModel{});

    ned::ui::Screen screen = ned::ui::Screen(0, 0);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = -1, .y_min = 0, .y_max = -1});
    view.Paint(canvas); // must not crash

    ned::ui::Screen normalScreen = ned::ui::Screen(30, 10);
    ned::ui::Canvas normalCanvas(normalScreen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 9});
    view.Paint(normalCanvas); // an empty model on a real canvas must not crash either
}

TEST_CASE("MemoryImageView paints each byte as a grayscale foreground/background pair via the half-block glyph",
          "[MemoryImageView]") {
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    ned::ui::MemoryImageView view(theme);
    // 2 bytes, one pixel row of 2 columns wide -- ComputeMemoryImageLayout(2, N)
    // picks 2 columns, 1 row, so this lands in the single top-only cell row
    // (no bottom pixel -- the cell's background falls back to theme_.background).
    view.SetModel(ned::ui::MemoryImageModel{.title = "t", .bytes = {0, 255}});

    ned::ui::Screen screen = ned::ui::Screen(20, 20);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 19});
    view.Paint(canvas);

    bool foundBlack = false;
    bool foundWhite = false;
    for (int y = 0; y < screen.Height(); ++y) {
        for (int x = 0; x < screen.Width(); ++x) {
            const ned::ui::Cell& cell = screen.PixelAt(x, y);
            if (cell.character == "\xE2\x96\x80") { // U+2580 UPPER HALF BLOCK
                if (cell.foreground_color == ned::ui::Color::RGB(0, 0, 0)) {
                    foundBlack = true;
                }
                if (cell.foreground_color == ned::ui::Color::RGB(255, 255, 255)) {
                    foundWhite = true;
                }
                REQUIRE(cell.background_color == theme.background); // no bottom pixel for either
            }
        }
    }
    REQUIRE(foundBlack);
    REQUIRE(foundWhite);
}

TEST_CASE("MemoryImageView dismisses on any key while focused", "[MemoryImageView]") {
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    ned::ui::MemoryImageView view(theme);
    view.SetModel(ned::ui::MemoryImageModel{.title = "t", .bytes = {1, 2, 3}});

    int cancels = 0;
    view.SetOnCancel([&cancels] { ++cancels; });

    // Not focused: OnEvent returns false (not consumed), no cancel.
    REQUIRE_FALSE(view.OnEvent(ned::ui::test::Character('q')));
    REQUIRE(cancels == 0);

    view.TakeFocus();
    REQUIRE(view.OnEvent(ned::ui::test::Character('q')));
    REQUIRE(cancels == 1);

    REQUIRE(view.OnEvent(ned::ui::test::Escape()));
    REQUIRE(cancels == 2);
}
