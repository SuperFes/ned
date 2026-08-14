#include <catch2/catch_test_macros.hpp>

#include <ox/ox.hpp>
#include <string>

#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
#include "UI/TabBar.h"
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

TEST_CASE("TabBar renders each open buffer as a tab, highlighting the active one", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    list.CreateBuffer("beta");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};
    tabBar.paint(canvas);

    const std::u32string row = RowText(screen, 0, 40);
    REQUIRE(row.find(U"alpha") != std::u32string::npos);
    REQUIRE(row.find(U"beta") != std::u32string::npos);

    // "alpha" (the active tab) starts at column 1 (after the leading space).
    REQUIRE(screen[{.x = 1, .y = 0}].brush == theme.activeTab);
    // "beta"'s tab uses the inactive brush.
    const std::size_t betaCol = row.find(U"beta");
    REQUIRE(screen[{.x = static_cast<int>(betaCol), .y = 0}].brush == theme.tabBar);
}

TEST_CASE("TabBar shows a modified marker on a tab whose buffer has unsaved changes", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    alpha.InsertAtPoint("x");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};
    tabBar.paint(canvas);

    REQUIRE(RowText(screen, 0, 40).find(U"alpha*") != std::u32string::npos);
}

TEST_CASE("mouse_press on a tab switches the active buffer", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    ned::text::Buffer&    beta  = list.CreateBuffer("beta");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    // Layout: " alpha ×" (cols 0-7), gap (8), " beta ×" starts at col 9;
    // clicking col 9 (the leading space) switches to it, not its own close
    // icon at col 15.
    tabBar.mouse_press(ox::Mouse{.at = {.x = 9, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(&activeBuffer.Get() == &beta);
}

TEST_CASE("mouse_press outside any tab is a no-op", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    list.CreateBuffer("beta");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    tabBar.mouse_press(ox::Mouse{.at = {.x = 39, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(&activeBuffer.Get() == &alpha);
}

TEST_CASE("The preview buffer's tab renders in italic, others don't", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    ned::text::Buffer&    beta  = list.CreateBuffer("beta");
    list.SetPreviewBuffer(&beta);

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};
    tabBar.paint(canvas);

    const std::u32string row      = RowText(screen, 0, 40);
    const std::size_t    alphaCol = row.find(U"alpha");
    const std::size_t    betaCol  = row.find(U"beta");
    REQUIRE_FALSE((screen[{.x = static_cast<int>(alphaCol), .y = 0}].brush.traits.contains(ox::Trait::Italic)));
    REQUIRE(screen[{.x = static_cast<int>(betaCol), .y = 0}].brush.traits.contains(ox::Trait::Italic));
}

TEST_CASE("A close icon is rendered at the end of each tab", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};
    tabBar.paint(canvas);

    // " alpha ×" -- the × sits at column 7.
    REQUIRE(screen[{.x = 7, .y = 0}].symbol == U'×');
}

TEST_CASE("Clicking a tab's close icon invokes the registered handler with that buffer", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    list.CreateBuffer("beta");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    ned::text::Buffer* closed = nullptr;
    tabBar.SetOnCloseRequest([&closed](ned::text::Buffer& buffer) { closed = &buffer; });

    // " alpha ×" -- × at column 7.
    tabBar.mouse_press(ox::Mouse{.at = {.x = 7, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(closed == &alpha);
    // Clicking the close icon does not itself switch the active buffer --
    // that decision belongs entirely to the registered handler.
    REQUIRE(&activeBuffer.Get() == &alpha);
}

TEST_CASE("Clicking a close icon with no handler registered is a safe no-op", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    tabBar.mouse_press(ox::Mouse{.at = {.x = 7, .y = 0}, .button = ox::Mouse::Button::Left}); // must not crash
}

TEST_CASE("An overflow indicator appears only on the edge(s) with scrolled-past content", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    first = list.CreateBuffer("first-buffer-name");
    for (int i = 0; i < 10; ++i) {
        list.CreateBuffer("buffer-" + std::to_string(i));
    }

    ned::ui::ActiveBuffer activeBuffer(first);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 20, .height = 1};

    ox::ScreenBuffer screen({.width = 20, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 1}};

    tabBar.paint(canvas);
    REQUIRE_FALSE(screen[{.x = 0, .y = 0}].symbol == U'‹'); // nothing scrolled past yet
    REQUIRE(screen[{.x = 19, .y = 0}].symbol == U'›');      // but more content overflows to the right

    for (int i = 0; i < 3; ++i) {
        tabBar.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    }
    tabBar.paint(canvas);
    REQUIRE(screen[{.x = 0, .y = 0}].symbol == U'‹'); // now scrolled past some content on the left too
}

TEST_CASE("mouse_wheel scrolls the tab bar horizontally when tabs overflow the width, and clamps", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    first = list.CreateBuffer("first-buffer-name");
    for (int i = 0; i < 10; ++i) {
        list.CreateBuffer("buffer-" + std::to_string(i));
    }

    ned::ui::ActiveBuffer activeBuffer(first);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 20, .height = 1}; // narrower than the total tab content

    ox::ScreenBuffer screen({.width = 20, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 20, .height = 1}};

    tabBar.paint(canvas);
    REQUIRE(RowText(screen, 0, 20).find(U"first-buffer-name") != std::u32string::npos);

    // Scroll right past the point where "first-buffer-name" is still visible.
    for (int i = 0; i < 10; ++i) {
        tabBar.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    }
    tabBar.paint(canvas);
    REQUIRE(RowText(screen, 0, 20).find(U"first-buffer-name") == std::u32string::npos);

    // Scroll back left all the way; clamps at 0, "first-buffer-name" is visible again.
    for (int i = 0; i < 20; ++i) {
        tabBar.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollUp});
    }
    tabBar.paint(canvas);
    REQUIRE(RowText(screen, 0, 20).find(U"first-buffer-name") != std::u32string::npos);
}

TEST_CASE("mouse_wheel is a no-op when the tabs already fit the viewport", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 40, .height = 1};

    ox::ScreenBuffer screen({.width = 40, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 40, .height = 1}};

    tabBar.paint(canvas);
    const std::u32string before = RowText(screen, 0, 40);

    tabBar.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    tabBar.paint(canvas);
    REQUIRE(RowText(screen, 0, 40) == before);
}

TEST_CASE("Stress: hundreds of tabs in a narrow viewport stay memory-safe under repeated paint/scroll/click",
          "[TabBar]") {
    // User report: opening "a butt tonne of files" made things "go crazy" --
    // reproducing the scale directly (well past anything manually tested so
    // far) rather than guessing, so any real corruption shows up under the
    // sanitizer instead of being assumed away.
    ned::text::BufferList list;
    for (int i = 0; i < 500; ++i) {
        list.CreateBuffer("file" + std::to_string(i) + ".txt");
    }
    ned::text::Buffer& first = *list.Buffers().front();

    ned::ui::ActiveBuffer activeBuffer(first);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    tabBar.size = {.width = 60, .height = 1};

    ox::ScreenBuffer screen({.width = 60, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 60, .height = 1}};

    for (int i = 0; i < 200; ++i) {
        tabBar.paint(canvas);
        tabBar.mouse_wheel(ox::Mouse{.at = {.x = 30, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    }
    for (int i = 0; i < 200; ++i) {
        tabBar.paint(canvas);
        tabBar.mouse_wheel(ox::Mouse{.at = {.x = 30, .y = 0}, .button = ox::Mouse::Button::ScrollUp});
    }
    for (int x = -10; x < 70; ++x) {
        tabBar.mouse_press(ox::Mouse{.at = {.x = x, .y = 0}, .button = ox::Mouse::Button::Left});
        tabBar.paint(canvas);
    }

    REQUIRE(list.Count() == 500); // survived without crashing or losing buffers
}
