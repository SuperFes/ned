#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <ox/ox.hpp>
#include <string>

#include "Editor/ProjectRoot.h"
#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
#include "UI/ProjectSidebar.h"
#include "UI/Theme.h"

namespace {

std::u32string RowText(ox::ScreenBuffer& screen, int row, int width) {
    std::u32string out;
    for (int col = 0; col < width; ++col) {
        out.push_back(screen[{.x = col, .y = row}].symbol);
    }
    return out;
}

// ProjectSidebar reads ned::editor::ProjectRoot() (project-root-detection
// follow-up; previously std::filesystem::current_path() directly), so tests
// need to temporarily relocate both the process's cwd and the project root
// to a controlled scratch directory -- both restored on scope exit even if
// a REQUIRE fails partway through. Mirrors BufferViewTest.cpp's own
// CurrentPathGuard exactly; duplicated rather than shared for something
// this small, same call as ProjectTree.cpp's IsDotDirectory duplicating
// ProjectSearch.cpp's own. ProjectRoot() itself needs explicit handling
// here, not just cwd: unlike cwd it's a lazily-initialized static that only
// captures std::filesystem::current_path() on its *first-ever* call within
// the process -- when every test case runs as its own ctest-registered
// process this happens to land correctly by sheer construction-order luck,
// but running the whole suite as one process (./build/ned_tests with no
// filter, an equally official way to run it per CLAUDE.md) does not
// initialize it fresh per test, so it must be set explicitly instead of
// relying on that coincidence.
class CurrentPathGuard {
  public:
    explicit CurrentPathGuard(const std::filesystem::path& newPath) : previous_(std::filesystem::current_path()), previousRoot_(ned::editor::ProjectRoot()) {
        std::filesystem::current_path(newPath);
        ned::editor::SetProjectRoot(newPath);
    }
    ~CurrentPathGuard() {
        std::filesystem::current_path(previous_);
        ned::editor::SetProjectRoot(previousRoot_);
    }
    CurrentPathGuard(const CurrentPathGuard&)            = delete;
    CurrentPathGuard& operator=(const CurrentPathGuard&) = delete;

  private:
    std::filesystem::path previous_;
    std::filesystem::path previousRoot_;
};

} // namespace

TEST_CASE("ProjectSidebar renders a collapsed tree by default, with a disclosure triangle on directories",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_render";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    {
        std::ofstream(dir / "sub" / "nested.txt") << "x";
    }
    {
        std::ofstream(dir / "top.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      buffer = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(buffer);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    ox::ScreenBuffer screen({.width = 28, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 28, .height = 5}};
    sidebar.paint(canvas);

    const std::u32string row0 = RowText(screen, 0, 28);
    const std::u32string row1 = RowText(screen, 1, 28);

    // "sub/" is collapsed by default -- its child never renders, and
    // "top.txt" (sub's only sibling) is the very next visible row.
    REQUIRE(row0.find(U"sub/") != std::u32string::npos);
    REQUIRE(row0.find(U'▸') != std::u32string::npos); // collapsed disclosure triangle
    REQUIRE(row1.find(U"top.txt") != std::u32string::npos);
    REQUIRE(RowText(screen, 2, 28).find(U"nested.txt") == std::u32string::npos);
    // Tree-connector lines (box-drawing, not plain-space indentation).
    REQUIRE((row0[0] == U'│' || row0[0] == U'└' || row0[0] == U'├'));

    std::filesystem::remove_all(dir);
}

TEST_CASE("Clicking a collapsed directory expands it, revealing indented children; clicking again collapses it",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_expand";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    {
        std::ofstream(dir / "sub" / "nested.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    ox::ScreenBuffer screen({.width = 28, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 28, .height = 5}};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // expand "sub/"
    sidebar.paint(canvas);

    const std::u32string row0 = RowText(screen, 0, 28);
    const std::u32string row1 = RowText(screen, 1, 28);
    REQUIRE(row0.find(U'▾') != std::u32string::npos); // expanded disclosure triangle
    REQUIRE(row1.find(U"nested.txt") != std::u32string::npos);
    // The nested entry's tree-connector prefix pushes its name at least as
    // far right as its parent's -- exactly equal in this case, since a
    // directory's own disclosure triangle ("▾ ") occupies the same two
    // columns a file one level deeper would otherwise need for its own
    // indent step.
    REQUIRE(row1.find(U"nested.txt") >= row0.find(U"sub/"));

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // collapse it again
    sidebar.paint(canvas);
    REQUIRE(RowText(screen, 1, 28).find(U"nested.txt") == std::u32string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ProjectSidebar highlights the entry matching the active buffer's file", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_highlight";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "x";
    }
    {
        std::ofstream(dir / "b.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      opened = list.OpenFile(dir / "a.txt");
    ned::ui::ActiveBuffer   activeBuffer(opened);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    ox::ScreenBuffer screen({.width = 28, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 28, .height = 5}};
    sidebar.paint(canvas);

    // a.txt sorts before b.txt -- row 0.
    REQUIRE(RowText(screen, 0, 28).find(U"a.txt") != std::u32string::npos);
    REQUIRE(screen[{.x = 0, .y = 0}].brush == theme.activeTab);
    REQUIRE(RowText(screen, 1, 28).find(U"b.txt") != std::u32string::npos);
    REQUIRE_FALSE(screen[{.x = 0, .y = 1}].brush == theme.activeTab);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Clicking a file entry opens it and switches the active buffer", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_click";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "target.txt") << "hello from disk";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(&activeBuffer.Get() != &scratch);
    REQUIRE(activeBuffer.Get().Text() == "hello from disk");
    REQUIRE(statusMessage.empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("Single-clicking a file marks it as the preview buffer", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_preview";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "target.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(list.PreviewBuffer() == &activeBuffer.Get());

    std::filesystem::remove_all(dir);
}

TEST_CASE("A second single click on a different file replaces the preview, closing the old one",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_preview_replace";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "x";
    }
    {
        std::ofstream(dir / "b.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // "a.txt"
    REQUIRE(list.PreviewBuffer() != nullptr);
    REQUIRE(list.Count() == 2); // scratch + a.txt

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 1}, .button = ox::Mouse::Button::Left}); // "b.txt"

    REQUIRE(list.Count() == 2);             // scratch + b.txt -- a.txt's preview was closed, not kept
    REQUIRE(list.Find("a.txt") == nullptr); // the old preview is gone
    REQUIRE(list.PreviewBuffer() == &activeBuffer.Get());
    REQUIRE(list.PreviewBuffer()->Name() == "b.txt"); // not merely "some pointer" -- the *new* preview specifically

    std::filesystem::remove_all(dir);
}

TEST_CASE("Double-clicking (two clicks on the same file) promotes the preview in place", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_doubleclick";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "target.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE(list.PreviewBuffer() != nullptr);

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // same file, rapid second click

    REQUIRE(list.PreviewBuffer() == nullptr); // promoted -- no longer just a preview
    REQUIRE(list.Count() == 2);               // scratch + target.txt, never duplicated

    std::filesystem::remove_all(dir);
}

TEST_CASE("Clicking an already-open, non-preview buffer switches to it without duplicating or touching preview state",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_reuse";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::text::Buffer&      already = list.OpenFile(dir / "a.txt"); // opened directly, not via a sidebar click
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left});

    REQUIRE(&activeBuffer.Get() == &already);
    REQUIRE(list.Count() == 2); // no duplicate buffer created
    REQUIRE(list.PreviewBuffer() == nullptr);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Clicking a directory entry toggles it without opening any buffer", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_clickdir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // "sub/"

    REQUIRE(&activeBuffer.Get() == &scratch);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Clicking past the end of the tree is a safe no-op", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_clickpast";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "only.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 4}, .button = ox::Mouse::Button::Left}); // past the one entry

    REQUIRE(&activeBuffer.Get() == &scratch);

    std::filesystem::remove_all(dir);
}

TEST_CASE("mouse_wheel scrolls the tree and clamps at both ends", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_wheel";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    for (int i = 0; i < 20; ++i) {
        std::ofstream(dir / (std::to_string(i) + ".txt")) << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5}; // fewer rows than the 20 files

    ox::ScreenBuffer screen({.width = 28, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 28, .height = 5}};

    sidebar.paint(canvas);
    REQUIRE(RowText(screen, 0, 28).find(U"0.txt") != std::u32string::npos);

    for (int i = 0; i < 10; ++i) {
        sidebar.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    }
    sidebar.paint(canvas);
    REQUIRE(RowText(screen, 0, 28).find(U"0.txt") == std::u32string::npos); // scrolled past it

    for (int i = 0; i < 20; ++i) {
        sidebar.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollUp});
    }
    sidebar.paint(canvas);
    REQUIRE(RowText(screen, 0, 28).find(U"0.txt") != std::u32string::npos); // clamped back to the top

    std::filesystem::remove_all(dir);
}

TEST_CASE("Scrolling past an expanded directory's own row pins it at the top (sticky scroll)", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_sticky";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    for (int i = 0; i < 10; ++i) {
        std::ofstream(dir / "sub" / (std::to_string(i) + ".txt")) << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // expand "sub/"
    for (int i = 0; i < 5; ++i) {
        sidebar.mouse_wheel(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::ScrollDown});
    }

    ox::ScreenBuffer screen({.width = 28, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 28, .height = 5}};
    sidebar.paint(canvas);

    // "sub/" itself has scrolled out of the ordinary content area, but stays
    // pinned as a sticky header on row 0 instead of disappearing.
    REQUIRE(RowText(screen, 0, 28).find(U"sub/") != std::u32string::npos);
    REQUIRE(screen[{.x = 0, .y = 0}].brush == theme.tabBar);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Pressing the divider column starts a resize instead of opening/toggling an entry", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_resizestart";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "only.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    REQUIRE_FALSE(sidebar.IsResizing());
    sidebar.mouse_press(ox::Mouse{.at = {.x = 27, .y = 0}, .button = ox::Mouse::Button::Left}); // divider column
    REQUIRE(sidebar.IsResizing());
    REQUIRE(&activeBuffer.Get() == &scratch); // did not open "only.txt"

    sidebar.mouse_release(ox::Mouse{.at = {.x = 27, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE_FALSE(sidebar.IsResizing());

    std::filesystem::remove_all(dir);
}

TEST_CASE("Shrinking the divider updates size_policy, anchored to the drag's total displacement", "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 20, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 19, .y = 0}, .button = ox::Mouse::Button::Left}); // divider column
    REQUIRE(sidebar.IsResizing());

    sidebar.mouse_move(ox::Mouse{.at = {.x = 10, .y = 0}}); // dragged 9 columns left of the press point
    REQUIRE(sidebar.size_policy.minimum == 11);
    REQUIRE(sidebar.size_policy.maximum == 11);

    // A second move is measured from the *original* press, not the previous
    // move -- dragging back out to x=19 (0 net displacement) restores the
    // starting width exactly, not some compounded value.
    sidebar.mouse_move(ox::Mouse{.at = {.x = 19, .y = 0}});
    REQUIRE(sidebar.size_policy.minimum == 20);
}

TEST_CASE("Dragging the divider clamps to a minimum width", "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 20, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 19, .y = 0}, .button = ox::Mouse::Button::Left});
    sidebar.mouse_move(ox::Mouse{.at = {.x = -1000, .y = 0}}); // absurdly far left

    REQUIRE(sidebar.size_policy.minimum > 0); // clamped, not driven negative or to zero
}

TEST_CASE("mouse_move without a resize in progress is a safe no-op", "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size                = {.width = 20, .height = 5};
    const ox::SizePolicy before = sidebar.size_policy;

    sidebar.mouse_move(ox::Mouse{.at = {.x = 5, .y = 0}}); // must not crash or change anything

    REQUIRE(sidebar.size_policy.minimum == before.minimum);
    REQUIRE(sidebar.size_policy.maximum == before.maximum);
}

TEST_CASE("Dragging the divider with a registered sidebarRow reflows widths immediately", "[ProjectSidebar]") {
    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;

    ox::Row row{
        ned::ui::ProjectSidebar(activeBuffer, list, statusMessage, theme) | ox::SizePolicy::fixed(20),
        ox::Widget{}, // stand-in for BufferView -- the flexible neighbor that reclaims freed space
    };
    auto& [sidebar, filler] = row.children;
    row.size                = {.width = 60, .height = 5};
    row.resize(row.size);

    REQUIRE(sidebar.size.width == 20);
    REQUIRE(filler.size.width == 40);

    sidebar.SetSidebarRow(&row);
    sidebar.mouse_press(ox::Mouse{.at = {.x = 19, .y = 0}, .button = ox::Mouse::Button::Left}); // divider column
    sidebar.mouse_move(ox::Mouse{.at = {.x = 10, .y = 0}});                                     // shrink by 9, still within the old bounds

    REQUIRE(sidebar.size.width == 11);
    REQUIRE(filler.size.width == 49);

    sidebar.mouse_release(ox::Mouse{.at = {.x = 10, .y = 0}, .button = ox::Mouse::Button::Left});
    REQUIRE_FALSE(sidebar.IsResizing());
}

TEST_CASE("RevealPath expands every ancestor directory down to the target file", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_reveal";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "src" / "nested");
    {
        std::ofstream(dir / "src" / "nested" / "file.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    ox::ScreenBuffer screen({.width = 28, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 28, .height = 5}};

    sidebar.paint(canvas);
    REQUIRE(RowText(screen, 1, 28).find(U"nested") == std::u32string::npos); // collapsed by default

    sidebar.RevealPath(dir / "src" / "nested" / "file.txt");
    sidebar.paint(canvas);

    // "src/" (row 0) and "nested/" (row 1) are both now expanded, so
    // "file.txt" (row 2) is directly visible without any manual clicking.
    REQUIRE(RowText(screen, 0, 28).find(U'▾') != std::u32string::npos); // "src/" expanded
    REQUIRE(RowText(screen, 1, 28).find(U"nested") != std::u32string::npos);
    REQUIRE(RowText(screen, 1, 28).find(U'▾') != std::u32string::npos); // "nested/" expanded too
    REQUIRE(RowText(screen, 2, 28).find(U"file.txt") != std::u32string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("RevealPath is a no-op when the target's own directory is already the root", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_reveal_flat";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.RevealPath(dir / "file.txt"); // must not crash -- nothing to expand

    ox::ScreenBuffer screen({.width = 28, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 28, .height = 5}};
    sidebar.paint(canvas);
    REQUIRE(RowText(screen, 0, 28).find(U"file.txt") != std::u32string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("RevealPath is a safe no-op for a path outside the current project root", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_reveal_outside";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "src");
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.RevealPath(std::filesystem::temp_directory_path() / "somewhere_else_entirely" / "file.txt");

    ox::ScreenBuffer screen({.width = 28, .height = 5});
    ox::Canvas       canvas{.buffer = screen, .at = {.x = 0, .y = 0}, .size = {.width = 28, .height = 5}};
    sidebar.paint(canvas);
    REQUIRE(RowText(screen, 0, 28).find(U'▸') != std::u32string::npos); // "src/" still collapsed

    std::filesystem::remove_all(dir);
}

TEST_CASE("A failed open reports an error via statusMessage without crashing", "[ProjectSidebar]") {
    // A real TOCTOU-style failure: the entry is listed fine (a regular file,
    // so it passes BuildProjectTree's own is_regular_file() check) but can't
    // actually be opened -- OpenOrCreateFile/Buffer::FromFile throws, and
    // ProjectSidebar::mouse_press must catch it rather than letting it
    // propagate.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_failopen";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path unreadable = dir / "unreadable.txt";
    {
        std::ofstream(unreadable) << "x";
    }

    std::error_code ec;
    std::filesystem::permissions(unreadable, std::filesystem::perms::none, ec);
    if (ec) {
        std::filesystem::remove_all(dir);
        return; // permission bits unsupported in this environment -- nothing to test
    }

    // Confirm the permission change actually took effect -- e.g. running as
    // root bypasses it entirely -- skip rather than false-fail if so.
    const bool stillReadable = static_cast<bool>(std::ifstream(unreadable));
    if (stillReadable) {
        std::filesystem::permissions(unreadable, std::filesystem::perms::owner_all, ec);
        std::filesystem::remove_all(dir);
        return;
    }

    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar(activeBuffer, list, statusMessage, theme);
    sidebar.size = {.width = 28, .height = 5};

    sidebar.mouse_press(ox::Mouse{.at = {.x = 0, .y = 0}, .button = ox::Mouse::Button::Left}); // must not crash

    REQUIRE(&activeBuffer.Get() == &scratch);
    REQUIRE_FALSE(statusMessage.empty());

    std::filesystem::permissions(unreadable, std::filesystem::perms::owner_all, ec);
    std::filesystem::remove_all(dir);
}
