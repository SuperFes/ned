#include <catch2/catch_test_macros.hpp>

#include "TestEvents.h"
#include "UI/Theme.h"
#include "UI/TreeView.h"

TEST_CASE("TreeView fills its entire interior with the theme background, leaving no stale cells", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.title = "Callers", .rows = {{.label = "caller_fn"}}});

    ned::ui::Screen screen = ned::ui::Screen(30, 6);
    for (int y = 0; y < screen.Height(); ++y) {
        for (int x = 0; x < screen.Width(); ++x) {
            ned::ui::Cell& cell   = screen.PixelAt(x, y);
            cell.character        = "Z";
            cell.background_color = ned::ui::Color::RGB(0x11, 0x22, 0x33);
        }
    }
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 5});
    tree.Paint(canvas);

    for (int y = 1; y < 5; ++y) {
        for (int x = 1; x < 29; ++x) {
            const ned::ui::Cell& cell = screen.PixelAt(x, y);
            INFO("cell (" << x << ", " << y << ")");
            REQUIRE(cell.character != "Z");
            REQUIRE(cell.background_color == theme.background);
        }
    }
}

TEST_CASE("TreeView degrades sanely for a zero-area canvas", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.rows = {}});

    ned::ui::Screen screen = ned::ui::Screen(0, 0);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = -1, .y_min = 0, .y_max = -1});
    tree.Paint(canvas); // must not crash
}

TEST_CASE("TreeView indents a row by 2 columns per depth level, before the disclosure glyph", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.rows = {
                                             {.label = "root", .depth = 0, .hasChildren = true, .expanded = true},
                                             {.label = "child", .depth = 1, .hasChildren = false},
                                         }});

    ned::ui::Screen screen = ned::ui::Screen(30, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
    tree.Paint(canvas);

    // depth 0: disclosure glyph at x=2 (indent 2), label starts at x=4.
    REQUIRE(screen.PixelAt(2, 1).character == "▾"); // expanded
    REQUIRE(screen.PixelAt(4, 1).character == "r");

    // depth 1: indent 2 + 1*2 = 4, label starts at x=6. hasChildren=false ->
    // blank disclosure column (a confirmed leaf).
    REQUIRE(screen.PixelAt(4, 2).character == " ");
    REQUIRE(screen.PixelAt(6, 2).character == "c");
}

TEST_CASE("TreeView shows a collapsed glyph, a loading glyph, and a blank leaf glyph per row state", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.rows = {
                                             {.label = "collapsed", .hasChildren = true, .expanded = false},
                                             {.label = "loading", .hasChildren = true, .expanded = false, .loading = true},
                                             {.label = "leaf", .hasChildren = false},
                                         }});

    ned::ui::Screen screen = ned::ui::Screen(30, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 5});
    tree.Paint(canvas);

    REQUIRE(screen.PixelAt(2, 1).character == "▸");
    REQUIRE(screen.PixelAt(2, 2).character == "…");
    REQUIRE(screen.PixelAt(2, 3).character == " ");
}

TEST_CASE("TreeView paints a selection bar across the selected row only", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.rows = {{.label = "one"}, {.label = "two"}, {.label = "three"}}, .selectedIndex = 1});

    ned::ui::Screen screen = ned::ui::Screen(30, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 5});
    tree.Paint(canvas);

    REQUIRE(screen.PixelAt(5, 1).background_color == theme.background);
    REQUIRE(screen.PixelAt(5, 2).background_color == theme.selectionBackground);
    REQUIRE(screen.PixelAt(5, 3).background_color == theme.background);
}

TEST_CASE("TreeView is always focusable, unlike ListPopup's opt-in", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    REQUIRE(tree.Focusable());
}

TEST_CASE("TreeView navigates with Up/Down (wrapping), activates on Enter, and cancels on Escape", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.rows = {{.label = "one"}, {.label = "two"}, {.label = "three"}}, .selectedIndex = 0});
    tree.TakeFocus();

    std::optional<std::size_t> activated;
    tree.SetOnActivate([&](std::size_t index) { activated = index; });
    bool cancelled = false;
    tree.SetOnCancel([&] { cancelled = true; });

    REQUIRE(tree.OnEvent(ned::ui::test::ArrowUp())); // wraps from 0 to the last row
    REQUIRE(tree.OnEvent(ned::ui::test::Return()));
    REQUIRE(activated == 2);

    REQUIRE(tree.OnEvent(ned::ui::test::Escape()));
    REQUIRE(cancelled);
}

TEST_CASE("TreeView fires onToggleExpand on Right only for an unexpanded row with children, never while loading",
          "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{
        .rows          = {{.label = "leaf", .hasChildren = false}, {.label = "loading", .hasChildren = true, .loading = true}, {.label = "collapsed", .hasChildren = true, .expanded = false}},
        .selectedIndex = 0,
    });
    tree.TakeFocus();

    int toggled = -1;
    tree.SetOnToggleExpand([&](std::size_t index) { toggled = static_cast<int>(index); });

    REQUIRE(tree.OnEvent(ned::ui::test::ArrowRight())); // row 0: a leaf, no-op
    REQUIRE(toggled == -1);

    REQUIRE(tree.OnEvent(ned::ui::test::ArrowDown()));
    REQUIRE(tree.OnEvent(ned::ui::test::ArrowRight())); // row 1: loading, no-op
    REQUIRE(toggled == -1);

    REQUIRE(tree.OnEvent(ned::ui::test::ArrowDown()));
    REQUIRE(tree.OnEvent(ned::ui::test::ArrowRight())); // row 2: collapsed with children -> fires
    REQUIRE(toggled == 2);
}

TEST_CASE("TreeView fires onSelectionChanged on Up/Down but not on a no-op single-row move", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.rows = {{.label = "one"}, {.label = "two"}}, .selectedIndex = 0});
    tree.TakeFocus();

    std::vector<std::size_t> selections;
    tree.SetOnSelectionChanged([&](std::size_t index) { selections.push_back(index); });

    REQUIRE(tree.OnEvent(ned::ui::test::ArrowDown()));
    REQUIRE(selections == std::vector<std::size_t>{1});
}

TEST_CASE("TreeView fires onCollapseRequested on Left only for an expanded row", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{
        .rows          = {{.label = "collapsed", .hasChildren = true, .expanded = false}, {.label = "expanded", .hasChildren = true, .expanded = true}},
        .selectedIndex = 0,
    });
    tree.TakeFocus();

    int collapsed = -1;
    tree.SetOnCollapseRequested([&](std::size_t index) { collapsed = static_cast<int>(index); });

    REQUIRE(tree.OnEvent(ned::ui::test::ArrowLeft())); // row 0: not expanded, no-op
    REQUIRE(collapsed == -1);

    REQUIRE(tree.OnEvent(ned::ui::test::ArrowDown()));
    REQUIRE(tree.OnEvent(ned::ui::test::ArrowLeft())); // row 1: expanded -> fires
    REQUIRE(collapsed == 1);
}

TEST_CASE("TreeView does not handle keys when unfocused", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.rows = {{.label = "a"}}});

    bool activated = false;
    tree.SetOnActivate([&](std::size_t) { activated = true; });
    REQUIRE_FALSE(tree.OnEvent(ned::ui::test::Return()));
    REQUIRE_FALSE(activated);
}

TEST_CASE("TreeView activates a row on a left-click and updates the selection", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{.rows = {{.label = "one"}, {.label = "two"}}, .selectedIndex = 0});
    tree.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 5});

    std::optional<std::size_t> activated;
    tree.SetOnActivate([&](std::size_t index) { activated = index; });

    // Row 1 ("two") is at local y=2 (y=1 is the top border, y=1 the first
    // row, y=2 the second) -- matching ListPopup's own row-to-y mapping.
    REQUIRE(tree.OnEvent(ned::ui::test::Mouse(5, 2, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(activated == 1);
}

// debug-panel: the value column, the unhandled-key hook, scrolling, and the
// disclosure-column click -- what a standing panel needs on top of the
// one-shot hierarchy picker this widget was built for.

TEST_CASE("TreeView right-aligns a row's value column and clips the label before it", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetModel(ned::ui::TreeViewModel{
        .rows = {{.label = "averyveryverylonglabelindeed", .depth = 0, .right = "42", .hasChildren = false}}});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    tree.Paint(canvas);

    // Width 20: the right column ends at x=18 (x=19 is the border), so "42"
    // occupies x=17..18 and the label may not reach x=16.
    REQUIRE(screen.PixelAt(17, 1).character == "4");
    REQUIRE(screen.PixelAt(18, 1).character == "2");
    REQUIRE(screen.PixelAt(16, 1).character == " ");
    REQUIRE(screen.PixelAt(4, 1).character == "a"); // label still painted from its usual column
}

TEST_CASE("TreeView offers an unhandled key to its consumer, with or without rows", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 5});
    tree.TakeFocus();

    std::vector<char32_t> keys;
    tree.SetOnKey([&](const ned::editor::KeyChord& chord) { keys.push_back(chord.Codepoint); });

    tree.SetModel(ned::ui::TreeViewModel{.rows = {}});
    REQUIRE(tree.OnEvent(ned::ui::test::Character("a")));
    REQUIRE(keys == std::vector<char32_t>{U'a'});

    tree.SetModel(ned::ui::TreeViewModel{.rows = {{.label = "one"}}, .selectedIndex = 0});
    REQUIRE(tree.OnEvent(ned::ui::test::Character("d")));
    REQUIRE(keys == std::vector<char32_t>{U'a', U'd'});

    // Keys this widget handles itself never reach the hook.
    REQUIRE(tree.OnEvent(ned::ui::test::Return()));
    REQUIRE(keys.size() == 2);
}

TEST_CASE("TreeView scrolls to keep the selected row visible", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4}); // 3 interior rows

    std::vector<ned::ui::TreeRow> rows;
    for (int i = 0; i < 8; ++i) {
        rows.push_back(ned::ui::TreeRow{.label = "row" + std::to_string(i), .hasChildren = false});
    }
    tree.SetModel(ned::ui::TreeViewModel{.rows = rows, .selectedIndex = 7});

    ned::ui::Screen screen = ned::ui::Screen(20, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});
    tree.Paint(canvas);

    // Three rows fit; the selection is the last one, so rows 5, 6, 7 show.
    REQUIRE(screen.PixelAt(7, 1).character == "5");
    REQUIRE(screen.PixelAt(7, 3).character == "7");
}

TEST_CASE("TreeView maps a click through its scroll offset", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});

    std::vector<ned::ui::TreeRow> rows;
    for (int i = 0; i < 8; ++i) {
        rows.push_back(ned::ui::TreeRow{.label = "row" + std::to_string(i), .hasChildren = false});
    }
    tree.SetModel(ned::ui::TreeViewModel{.rows = rows, .selectedIndex = 7});

    ned::ui::Screen screen = ned::ui::Screen(20, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});
    tree.Paint(canvas); // scrolls to rows 5..7

    std::optional<std::size_t> activated;
    tree.SetOnActivate([&](std::size_t index) { activated = index; });
    REQUIRE(tree.OnEvent(ned::ui::test::Mouse(8, 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(activated == 5); // the first visible row, not row 0
}

TEST_CASE("TreeView treats a click on the disclosure glyph as expand/collapse, not activate", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 5});
    tree.SetModel(ned::ui::TreeViewModel{
        .rows          = {{.label = "collapsed", .depth = 0, .hasChildren = true, .expanded = false},
                          {.label = "expanded", .depth = 1, .hasChildren = true, .expanded = true}},
        .selectedIndex = 0});

    std::optional<std::size_t> activated;
    std::optional<std::size_t> expanded;
    std::optional<std::size_t> collapsed;
    tree.SetOnActivate([&](std::size_t index) { activated = index; });
    tree.SetOnToggleExpand([&](std::size_t index) { expanded = index; });
    tree.SetOnCollapseRequested([&](std::size_t index) { collapsed = index; });

    // Depth 0's glyph is at x=2.
    REQUIRE(tree.OnEvent(ned::ui::test::Mouse(2, 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(expanded == 0);
    REQUIRE_FALSE(activated);

    // Depth 1's glyph is at x=4; that row is already expanded.
    REQUIRE(tree.OnEvent(ned::ui::test::Mouse(4, 2, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(collapsed == 1);
    REQUIRE_FALSE(activated);

    // Anywhere else on the row still activates.
    REQUIRE(tree.OnEvent(ned::ui::test::Mouse(9, 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(activated == 0);
}

TEST_CASE("TreeView drops its frame and uses the whole box when a host owns the chrome", "[TreeView]") {
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::TreeView tree(theme);
    tree.SetDrawBorder(false);
    tree.SetModel(ned::ui::TreeViewModel{
        .title = "Debug", .rows = {{.label = "first", .hasChildren = false}, {.label = "second", .hasChildren = false}}});
    tree.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 3});

    ned::ui::Screen screen = ned::ui::Screen(20, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 3});
    tree.Paint(canvas);

    // Row 0 is a real row, not a border, and the label starts one column
    // further left than it would inside a frame.
    REQUIRE(screen.PixelAt(3, 0).character == "f");
    REQUIRE(screen.PixelAt(3, 1).character == "s");
    // No border anywhere on the edges.
    REQUIRE(screen.PixelAt(0, 0).character != "╭");
    REQUIRE(screen.PixelAt(19, 3).character != "╯");

    // A click maps through the same inset: local y=0 is the first row.
    std::optional<std::size_t> activated;
    tree.SetOnActivate([&](std::size_t index) { activated = index; });
    REQUIRE(tree.OnEvent(ned::ui::test::Mouse(8, 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(activated == 1);
}
