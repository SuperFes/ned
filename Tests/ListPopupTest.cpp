#include <catch2/catch_test_macros.hpp>

#include "Editor/Key.h"
#include "TestEvents.h"
#include "UI/ListPopup.h"
#include "UI/Theme.h"

TEST_CASE("ListPopup fills its entire interior with the theme background, leaving no stale cells", "[ListPopup]") {
    ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::ListPopup popup(theme);
    popup.SetModel(ned::ui::ListPopupModel{.title = "C-x-", .rows = {{.left = "o", .main = "other-window", .accented = true}}});

    ned::ui::Screen screen = ned::ui::Screen(30, 6);
    for (int y = 0; y < screen.Height(); ++y) {
        for (int x = 0; x < screen.Width(); ++x) {
            ned::ui::Cell& cell   = screen.PixelAt(x, y);
            cell.character        = "Z";
            cell.background_color = ned::ui::Color::RGB(0x11, 0x22, 0x33);
        }
    }
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

TEST_CASE("ListPopup renders a multi-byte UTF-8 glyph as one cell, not split across cells", "[ListPopup]") {
    // UTF-8-row-text follow-up: row text used to be written one *byte* per
    // cell (fine for plain-ASCII key chords/labels), which split a
    // multi-byte glyph like U+2191 (an up arrow, marking scrolled-off
    // candidates above the visible window) across several cells as garbage
    // -- confirmed live.
    ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::ListPopup popup(theme);
    popup.SetModel(ned::ui::ListPopupModel{.rows = {{.main = "↑ 3 more above"}}});

    ned::ui::Screen screen = ned::ui::Screen(30, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 3});
    popup.Paint(canvas);

    // Row 1 is the only content row; the main column starts at x=4 (x=2 is
    // the left column's own start, empty here, plus the fixed 2-column gap
    // before main) -- the arrow glyph should occupy exactly that one cell,
    // not split across it and the next.
    REQUIRE(screen.PixelAt(4, 1).character == "↑");
    REQUIRE(screen.PixelAt(5, 1).character == " ");
    REQUIRE(screen.PixelAt(6, 1).character == "3");
}

TEST_CASE("ListPopup degrades sanely for a zero-area canvas", "[ListPopup]") {
    ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::ListPopup popup(theme);
    popup.SetModel(ned::ui::ListPopupModel{.title = "C-x-", .rows = {}});

    ned::ui::Screen screen = ned::ui::Screen(0, 0);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = -1, .y_min = 0, .y_max = -1});
    popup.Paint(canvas); // must not crash
}

TEST_CASE("ListPopup paints a selection bar across the selected row only", "[ListPopup]") {
    ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::ListPopup popup(theme);
    popup.SetModel(ned::ui::ListPopupModel{
        .title         = "Buffers",
        .rows          = {{.main = "one"}, {.main = "two"}, {.main = "three"}},
        .selectedIndex = 1,
    });

    ned::ui::Screen screen = ned::ui::Screen(30, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 5});
    popup.Paint(canvas);

    // Row 1 (y=2, the second content row) is selected -> selectionBackground;
    // rows 0 and 2 (y=1, y=3) stay the plain background.
    REQUIRE(screen.PixelAt(5, 1).background_color == theme.background);
    REQUIRE(screen.PixelAt(5, 2).background_color == theme.selectionBackground);
    REQUIRE(screen.PixelAt(5, 3).background_color == theme.background);
}

TEST_CASE("ListPopup non-focusable mode never handles keys, even if focused", "[ListPopup]") {
    ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::ListPopup popup(theme);
    popup.SetModel(ned::ui::ListPopupModel{.rows = {{.main = "a"}, {.main = "b"}}});

    REQUIRE_FALSE(popup.Focusable());
    bool cancelled = false;
    popup.SetOnCancel([&] { cancelled = true; });
    REQUIRE_FALSE(popup.OnEvent(ned::ui::test::Escape()));
    REQUIRE_FALSE(cancelled);
}

TEST_CASE("ListPopup focus mode navigates, digit-selects, activates, and cancels", "[ListPopup]") {
    ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::ListPopup popup(theme);
    popup.SetModel(ned::ui::ListPopupModel{.rows = {{.main = "one"}, {.main = "two"}, {.main = "three"}}, .selectedIndex = 0});
    popup.SetFocusable(true);
    REQUIRE(popup.Focusable());
    popup.TakeFocus();

    std::optional<std::size_t> highlighted;
    popup.SetOnHighlightChange([&](std::size_t index) { highlighted = index; });
    std::optional<std::size_t> activated;
    popup.SetOnActivate([&](std::size_t index) { activated = index; });
    bool cancelled = false;
    popup.SetOnCancel([&] { cancelled = true; });
    int unhandledCount = 0;
    popup.SetOnKey([&](const ned::editor::KeyChord&) { ++unhandledCount; });

    REQUIRE(popup.OnEvent(ned::ui::test::ArrowDown()));
    REQUIRE(highlighted == 1);

    REQUIRE(popup.OnEvent(ned::ui::test::Character('3')));
    REQUIRE(activated == 2);

    REQUIRE(popup.OnEvent(ned::ui::test::Character('g')));
    REQUIRE(unhandledCount == 1);

    REQUIRE(popup.OnEvent(ned::ui::test::Escape()));
    REQUIRE(cancelled);
}
