#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/screen.hpp>
#include <string>

#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
#include "UI/TabBar.h"
#include "UI/Theme.h"

namespace {

std::string RowText(ftxui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

ftxui::Event MousePress(int x, int y) {
    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x      = x;
    mouse.y      = y;
    return ftxui::Event::Mouse("", mouse);
}

ftxui::Event MouseWheel(int x, int y, ftxui::Mouse::Button button) {
    ftxui::Mouse mouse;
    mouse.button = button;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x      = x;
    mouse.y      = y;
    return ftxui::Event::Mouse("", mouse);
}

void PlaceRow(ned::ui::TabBar& tabBar, int width) {
    tabBar.SetBox_(ftxui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 0});
}

} // namespace

TEST_CASE("TabBar renders each open buffer as a tab, highlighting the active one", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    list.CreateBuffer("beta");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    PlaceRow(tabBar, 40);

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    tabBar.Paint(canvas);

    const std::string row = RowText(screen, 0, 40);
    REQUIRE(row.find("alpha") != std::string::npos);
    REQUIRE(row.find("beta") != std::string::npos);

    // "alpha" (the active tab) starts at column 1 (after the leading space).
    REQUIRE(screen.PixelAt(1, 0).foreground_color == theme.activeTab.foreground.ToFtxui());
    // "beta"'s tab uses the inactive brush.
    const std::size_t betaCol = row.find("beta");
    REQUIRE(screen.PixelAt(static_cast<int>(betaCol), 0).foreground_color == theme.tabBar.foreground.ToFtxui());
}

TEST_CASE("TabBar shows a modified marker on a tab whose buffer has unsaved changes", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    alpha.InsertAtPoint("x");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    PlaceRow(tabBar, 40);

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    tabBar.Paint(canvas);

    REQUIRE(RowText(screen, 0, 40).find("alpha*") != std::string::npos);
}

TEST_CASE("A press on a tab switches the active buffer", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    ned::text::Buffer&    beta  = list.CreateBuffer("beta");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    PlaceRow(tabBar, 40);

    // Layout: " alpha ×" (cols 0-7), gap (8), " beta ×" starts at col 9;
    // clicking col 9 (the leading space) switches to it, not its own close
    // icon at col 15.
    tabBar.OnEvent(MousePress(9, 0));

    REQUIRE(&activeBuffer.Get() == &beta);
}

TEST_CASE("A press outside any tab is a no-op", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    list.CreateBuffer("beta");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    PlaceRow(tabBar, 40);

    tabBar.OnEvent(MousePress(39, 0));

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
    PlaceRow(tabBar, 40);

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    tabBar.Paint(canvas);

    const std::string row      = RowText(screen, 0, 40);
    const std::size_t alphaCol = row.find("alpha");
    const std::size_t betaCol  = row.find("beta");
    REQUIRE_FALSE(screen.PixelAt(static_cast<int>(alphaCol), 0).italic);
    REQUIRE(screen.PixelAt(static_cast<int>(betaCol), 0).italic);
}

TEST_CASE("A close icon is rendered at the end of each tab", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    PlaceRow(tabBar, 40);

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    tabBar.Paint(canvas);

    // " alpha ×" -- the × sits at column 7.
    REQUIRE(screen.PixelAt(7, 0).character == "×");
}

TEST_CASE("Clicking a tab's close icon invokes the registered handler with that buffer", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");
    list.CreateBuffer("beta");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    PlaceRow(tabBar, 40);

    ned::text::Buffer* closed = nullptr;
    tabBar.SetOnCloseRequest([&closed](ned::text::Buffer& buffer) { closed = &buffer; });

    // " alpha ×" -- × at column 7.
    tabBar.OnEvent(MousePress(7, 0));

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
    PlaceRow(tabBar, 40);

    tabBar.OnEvent(MousePress(7, 0)); // must not crash
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
    PlaceRow(tabBar, 20);

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    tabBar.Paint(canvas);
    REQUIRE_FALSE(screen.PixelAt(0, 0).character == "‹"); // nothing scrolled past yet
    REQUIRE(screen.PixelAt(19, 0).character == "›");      // but more content overflows to the right

    for (int i = 0; i < 3; ++i) {
        tabBar.OnEvent(MouseWheel(0, 0, ftxui::Mouse::WheelDown));
    }
    tabBar.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).character == "‹"); // now scrolled past some content on the left too
}

TEST_CASE("Wheel scrolls the tab bar horizontally when tabs overflow the width, and clamps", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    first = list.CreateBuffer("first-buffer-name");
    for (int i = 0; i < 10; ++i) {
        list.CreateBuffer("buffer-" + std::to_string(i));
    }

    ned::ui::ActiveBuffer activeBuffer(first);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    PlaceRow(tabBar, 20); // narrower than the total tab content

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    tabBar.Paint(canvas);
    REQUIRE(RowText(screen, 0, 20).find("first-buffer-name") != std::string::npos);

    // Scroll right past the point where "first-buffer-name" is still visible.
    for (int i = 0; i < 10; ++i) {
        tabBar.OnEvent(MouseWheel(0, 0, ftxui::Mouse::WheelDown));
    }
    tabBar.Paint(canvas);
    REQUIRE(RowText(screen, 0, 20).find("first-buffer-name") == std::string::npos);

    // Scroll back left all the way; clamps at 0, "first-buffer-name" is visible again.
    for (int i = 0; i < 20; ++i) {
        tabBar.OnEvent(MouseWheel(0, 0, ftxui::Mouse::WheelUp));
    }
    tabBar.Paint(canvas);
    REQUIRE(RowText(screen, 0, 20).find("first-buffer-name") != std::string::npos);
}

TEST_CASE("Wheel is a no-op when the tabs already fit the viewport", "[TabBar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    alpha = list.CreateBuffer("alpha");

    ned::ui::ActiveBuffer activeBuffer(alpha);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::TabBar       tabBar(activeBuffer, list, theme);
    PlaceRow(tabBar, 40);

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    tabBar.Paint(canvas);
    const std::string before = RowText(screen, 0, 40);

    tabBar.OnEvent(MouseWheel(0, 0, ftxui::Mouse::WheelDown));
    tabBar.Paint(canvas);
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
    PlaceRow(tabBar, 60);

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    for (int i = 0; i < 200; ++i) {
        tabBar.Paint(canvas);
        tabBar.OnEvent(MouseWheel(30, 0, ftxui::Mouse::WheelDown));
    }
    for (int i = 0; i < 200; ++i) {
        tabBar.Paint(canvas);
        tabBar.OnEvent(MouseWheel(30, 0, ftxui::Mouse::WheelUp));
    }
    for (int x = -10; x < 70; ++x) {
        tabBar.OnEvent(MousePress(x, 0));
        tabBar.Paint(canvas);
    }

    REQUIRE(list.Count() == 500); // survived without crashing or losing buffers
}
