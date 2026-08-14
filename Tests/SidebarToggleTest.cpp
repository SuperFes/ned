#include <catch2/catch_test_macros.hpp>

#include <ox/ox.hpp>
#include <string>

#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
#include "UI/ProjectSidebar.h"
#include "UI/SidebarToggle.h"
#include "UI/Theme.h"

TEST_CASE("SidebarToggle paints the closed symbol before any sidebar is registered", "[SidebarToggle]") {
    const ox::Brush        brush{.foreground = ox::XColor::BrightBlack};
    ned::ui::SidebarToggle toggle(brush);
    toggle.size = {.width = 1, .height = 1};

    ox::ScreenBuffer screen({.width = 1, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 1, .height = 1}};
    toggle.paint(canvas);

    REQUIRE(screen[{.x = 0, .y = 0}].symbol == U'»');
    REQUIRE(screen[{.x = 0, .y = 0}].brush == brush);
}

TEST_CASE("mouse_press with no sidebar registered is a safe no-op", "[SidebarToggle]") {
    const ox::Brush        brush{};
    ned::ui::SidebarToggle toggle(brush);

    toggle.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // must not crash
}

TEST_CASE("SidebarToggle's symbol tracks the registered sidebar's active flag", "[SidebarToggle]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);

    const ox::Brush        brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.size = {.width = 1, .height = 1};
    toggle.SetSidebar(&sidebar);

    ox::ScreenBuffer screen({.width = 1, .height = 1});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 1, .height = 1}};

    REQUIRE(sidebar.active); // ox::Widget::active defaults to true
    toggle.paint(canvas);
    REQUIRE(screen[{.x = 0, .y = 0}].symbol == U'«');

    sidebar.active = false;
    toggle.paint(canvas);
    REQUIRE(screen[{.x = 0, .y = 0}].symbol == U'»');
}

TEST_CASE("Left mouse_press flips the registered sidebar's active flag", "[SidebarToggle]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);

    const ox::Brush        brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetSidebar(&sidebar);

    REQUIRE(sidebar.active);
    toggle.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE_FALSE(sidebar.active);
    toggle.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(sidebar.active);
}

TEST_CASE("Left mouse_press with a registered sidebarRow reflows widths immediately, not just on the next terminal resize",
          "[SidebarToggle]") {
    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;

    const ox::Brush brush{};
    ox::Row         row{
        ned::ui::SidebarToggle(brush) | ox::SizePolicy::fixed(1),
        ned::ui::ProjectSidebar(activeBuffer, list, statusMessage, theme) | ox::SizePolicy::fixed(20),
        ox::Widget{}, // stand-in for BufferView -- the flexible neighbor that reclaims freed space
    };
    auto& [toggle, sidebar, filler] = row.children;
    row.size                        = {.width = 30, .height = 3};
    row.resize(row.size);

    REQUIRE(filler.size.width == 9);

    toggle.SetSidebar(&sidebar);
    toggle.SetSidebarRow(&row);
    toggle.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE_FALSE(sidebar.active);
    // The filler reclaimed the sidebar's 20 columns without any separate
    // resize() call from the test -- proving SetSidebarRow's own resize()
    // call, not just the .active flip, is what made this happen.
    REQUIRE(filler.size.width == 29);
}

TEST_CASE("mouse_release ends an in-progress sidebar resize even when the cursor ends up over this widget",
          "[SidebarToggle]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 20, .height = 3};

    const ox::Brush        brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetSidebar(&sidebar);

    // A shrinking drag that ends up back over SidebarToggle's own column --
    // without this widget also handling mouse_release, IsResizing() would
    // stay stuck true forever (no mouse-capture in TermOx means the release
    // is simply hit-tested to wherever the cursor happens to be).
    sidebar.mouse_press(ox::Mouse{.at = {.x = 19, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(sidebar.IsResizing());

    toggle.mouse_release(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE_FALSE(sidebar.IsResizing());
}

TEST_CASE("mouse_press with a non-Left button does not flip the sidebar", "[SidebarToggle]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);

    const ox::Brush        brush{};
    ned::ui::SidebarToggle toggle(brush);
    toggle.SetSidebar(&sidebar);

    toggle.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Right});
    REQUIRE(sidebar.active);
}
