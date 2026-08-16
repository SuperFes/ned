#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/screen.hpp>
#include <string>

#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
#include "UI/ProjectSidebar.h"
#include "UI/SidebarToggle.h"
#include "UI/Theme.h"

namespace {

ftxui::Event MousePress(int x, int y, ftxui::Mouse::Button button = ftxui::Mouse::Left) {
    ftxui::Mouse mouse;
    mouse.button = button;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x      = x;
    mouse.y      = y;
    return ftxui::Event::Mouse("", mouse);
}

ftxui::Event MouseRelease(int x, int y) {
    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Released;
    mouse.x      = x;
    mouse.y      = y;
    return ftxui::Event::Mouse("", mouse);
}

} // namespace

TEST_CASE("SidebarToggle paints the closed symbol before any sidebar is registered", "[SidebarToggle]") {
    const ned::ui::Brush   brush{.foreground = ned::ui::Color::BrightBlack};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetBox_(ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    toggle.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 0).character == "»");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == brush.foreground.ToFtxui());
}

TEST_CASE("mouse press with no sidebar registered is a safe no-op", "[SidebarToggle]") {
    const ned::ui::Brush   brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetBox_(ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});

    toggle.OnEvent(MousePress(0, 0)); // must not crash
}

TEST_CASE("SidebarToggle's symbol tracks the registered sidebar's active flag", "[SidebarToggle]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);

    const ned::ui::Brush   brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetBox_(ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    toggle.SetSidebar(&sidebar);

    ftxui::Screen   screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    ned::ui::Canvas canvas(screen, ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});

    REQUIRE(sidebar.active); // Widget::active defaults to true
    toggle.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).character == "«");

    sidebar.active = false;
    toggle.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).character == "»");
}

TEST_CASE("Left press flips the registered sidebar's active flag", "[SidebarToggle]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);

    const ned::ui::Brush   brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetBox_(ftxui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    toggle.SetSidebar(&sidebar);

    REQUIRE(sidebar.active);
    toggle.OnEvent(MousePress(0, 0));
    REQUIRE_FALSE(sidebar.active);
    toggle.OnEvent(MousePress(0, 0));
    REQUIRE(sidebar.active);
}

// The pre-migration "reflows widths immediately" test doesn't have an
// equivalent anymore: it existed specifically to verify SetSidebarRow's own
// forced-reflow workaround, which TermOx needed (flipping a plain field
// never triggered a relayout on its own) but FTXUI doesn't -- confirmed
// empirically during the migration (a real spike: toggling a child's
// inclusion in an hbox and letting the very next frame render naturally was
// enough for siblings to reclaim/cede the space). SetSidebarRow itself was
// removed along with the workaround it existed for; the underlying
// behavior -- a hidden sidebar's space actually getting reclaimed -- is
// exercised at the composition level once main.cpp's real widget tree is
// wired up, not as a SidebarToggle-level unit test.

TEST_CASE("A release ends an in-progress sidebar resize even when the cursor ends up over this widget",
          "[SidebarToggle]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    sidebar.SetBox_(ftxui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    const ned::ui::Brush   brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetSidebar(&sidebar);

    // A shrinking drag that ends up back over SidebarToggle's own column --
    // without this widget also handling a release, IsResizing() would stay
    // stuck true forever (FTXUI has no mouse-capture concept either: every
    // mouse event, including release, is delivered to every leaf widget
    // regardless of position; see Widget.h's own header comment).
    sidebar.OnEvent(MousePress(19, 0));
    REQUIRE(sidebar.IsResizing());

    toggle.OnEvent(MouseRelease(0, 0));
    REQUIRE_FALSE(sidebar.IsResizing());
}

TEST_CASE("mouse press with a non-Left button does not flip the sidebar", "[SidebarToggle]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);

    const ned::ui::Brush   brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetSidebar(&sidebar);

    toggle.OnEvent(MousePress(0, 0, ftxui::Mouse::Right));
    REQUIRE(sidebar.active);
}
