#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Editor/Project/Root.h"
#include "TestEvents.h"
#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
#include "UI/LeftDock.h"
#include "UI/ProjectSidebar.h"
#include "UI/Theme.h"

namespace {

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

// RowText concatenates each cell's (possibly multi-byte UTF-8) character
// into one std::string, so std::string::find on its result returns a BYTE
// offset, not a column -- meaningless to compare across two rows whose
// tree-connector prefixes (│├└▸▾, all 3-byte glyphs) differ in how many
// multi-byte characters precede the target text. This scans cell-by-cell
// instead, returning a true column index (or -1).
int ColumnOf(ned::ui::Screen& screen, int row, int width, const std::string& target) {
    for (int start = 0; start < width; ++start) {
        std::string joined;
        for (int col = start; col < width && joined.size() < target.size(); ++col) {
            joined += screen.PixelAt(col, row).character;
        }
        if (joined == target) {
            return start;
        }
    }
    return -1;
}

ned::ui::Event MousePress(int x, int y, ned::ui::MouseEvent::Button button = ned::ui::MouseEvent::Button::Left) {
    return ned::ui::test::Mouse(x, y, button, ned::ui::MouseEvent::Motion::Pressed);
}

ned::ui::Event MouseRelease(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released);
}

ned::ui::Event MouseMove(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved);
}

ned::ui::Event MouseWheel(int x, int y, ned::ui::MouseEvent::Button button) {
    return ned::ui::test::Mouse(x, y, button, ned::ui::MouseEvent::Motion::Pressed);
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

void PlaceSidebar(ned::ui::ProjectSidebar& sidebar, int width, int height) {
    sidebar.SetBox_(ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = height - 1});
}

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});
    sidebar.Paint(canvas);

    // Row 0 is always the project-name header (sidebar-header follow-up;
    // unified-left-dock follow-up: this widget's own content row now, not a
    // border title -- LeftDock owns the border) -- tree content starts at
    // row 1. dir's own filename is longer than the 27-column content width,
    // so it renders truncated -- checking a guaranteed-to-fit prefix rather
    // than the full name.
    REQUIRE(RowText(screen, 0, 28).find("ned_project_sidebar") != std::string::npos);

    const std::string row1 = RowText(screen, 1, 28);
    const std::string row2 = RowText(screen, 2, 28);

    // "sub/" is collapsed by default -- its child never renders, and
    // "top.txt" (sub's only sibling) is the very next visible row.
    REQUIRE(row1.find("sub/") != std::string::npos);
    REQUIRE(row1.find("▸") != std::string::npos); // collapsed disclosure triangle
    REQUIRE(row2.find("top.txt") != std::string::npos);
    REQUIRE(RowText(screen, 3, 28).find("nested.txt") == std::string::npos);
    // Tree-connector lines (box-drawing, not plain-space indentation) --
    // content starts at column 0 now, no frame to inset for (unified-
    // left-dock follow-up: LeftDock owns the border/inset instead).
    const std::string firstChar = screen.PixelAt(0, 1).character;
    REQUIRE((firstChar == "│" || firstChar == "└" || firstChar == "├"));

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});

    sidebar.OnEvent(MousePress(0, 1)); // expand "sub/" (row 1 -- row 0 is the header)
    sidebar.Paint(canvas);

    const std::string row1 = RowText(screen, 1, 28);
    const std::string row2 = RowText(screen, 2, 28);
    REQUIRE(row1.find("▾") != std::string::npos); // expanded disclosure triangle
    REQUIRE(row2.find("nested.txt") != std::string::npos);
    // The nested entry's tree-connector prefix pushes its name at least as
    // far right as its parent's -- exactly equal in this case, since a
    // directory's own disclosure triangle ("▾ ") occupies the same two
    // columns a file one level deeper would otherwise need for its own
    // indent step. Compared by real column (ColumnOf), not std::string::find
    // -- RowText's concatenated string mixes multi-byte tree-connector
    // glyphs with plain ASCII, so find() returns byte offsets that don't
    // correspond to columns once the two rows' prefixes contain a different
    // number of multi-byte characters.
    REQUIRE(ColumnOf(screen, 2, 28, "nested.txt") >= ColumnOf(screen, 1, 28, "sub/"));

    sidebar.OnEvent(MousePress(0, 1)); // collapse it again
    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 2, 28).find("nested.txt") == std::string::npos);

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});
    sidebar.Paint(canvas);

    // a.txt sorts before b.txt -- row 1 (row 0 is the header); content
    // starts at column 0, no frame to inset for.
    REQUIRE(RowText(screen, 1, 28).find("a.txt") != std::string::npos);
    REQUIRE(screen.PixelAt(0, 1).foreground_color == theme.activeTab.foreground);
    REQUIRE(RowText(screen, 2, 28).find("b.txt") != std::string::npos);
    REQUIRE_FALSE(screen.PixelAt(0, 2).foreground_color == theme.activeTab.foreground);

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // row 0 is the header

    REQUIRE(&activeBuffer.Get() != &scratch);
    REQUIRE(activeBuffer.Get().Text() == "hello from disk");
    REQUIRE(statusMessage.empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("Pressing a file entry arms DraggingFilePath, and a release back on this widget clears it", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_dragpath";
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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    REQUIRE_FALSE(sidebar.DraggingFilePath().has_value());
    sidebar.OnEvent(MousePress(0, 1));
    REQUIRE(sidebar.DraggingFilePath().has_value());
    REQUIRE(sidebar.DraggingFilePath()->filename() == "target.txt");

    // A plain click's release lands back on this widget's own bounds --
    // BufferView never gets involved, this alone clears the armed drag.
    sidebar.OnEvent(MouseRelease(0, 1));
    REQUIRE_FALSE(sidebar.DraggingFilePath().has_value());

    std::filesystem::remove_all(dir);
}

TEST_CASE("Pressing a directory entry never arms DraggingFilePath", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_dragpath_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // "sub/" -- toggles, never a drag source
    REQUIRE_FALSE(sidebar.DraggingFilePath().has_value());

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // row 0 is the header

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // "a.txt" (row 0 is the header)
    REQUIRE(list.PreviewBuffer() != nullptr);
    REQUIRE(list.Count() == 2); // scratch + a.txt

    sidebar.OnEvent(MousePress(0, 2)); // "b.txt"

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // row 0 is the header
    REQUIRE(list.PreviewBuffer() != nullptr);

    sidebar.OnEvent(MousePress(0, 1)); // same file, rapid second click

    REQUIRE(list.PreviewBuffer() == nullptr); // promoted -- no longer just a preview
    REQUIRE(list.Count() == 2);               // scratch + target.txt, never duplicated

    std::filesystem::remove_all(dir);
}

TEST_CASE("Re-clicking a still-preview file resets point to the top instead of carrying over where it was left",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_preview_reclick";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "target.txt") << "line one\nline two\nline three\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // single click -- opens target.txt as a preview (row 0 is the header)
    REQUIRE(list.PreviewBuffer() == &activeBuffer.Get());

    // "Tooling around" -- point moves away from the top without ever
    // modifying the buffer, so it stays a preview.
    activeBuffer.Get().SetPoint(activeBuffer.Get().Content().ByteLength());
    REQUIRE(!activeBuffer.Get().Modified());

    // A genuinely separate click, not a rapid double click -- past
    // kDoubleClickWindow, so this must not promote the preview.
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    sidebar.OnEvent(MousePress(0, 1));

    REQUIRE(list.PreviewBuffer() == &activeBuffer.Get()); // still just a preview, not promoted
    REQUIRE(activeBuffer.Get().Point() == 0);             // back to the top, not wherever it was left

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // row 0 is the header

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // "sub/" (row 0 is the header)

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 4)); // past the one entry

    REQUIRE(&activeBuffer.Get() == &scratch);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Wheel scrolls the tree and clamps at both ends", "[ProjectSidebar]") {
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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5); // fewer rows than the 20 files

    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});

    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 1, 28).find("0.txt") != std::string::npos); // row 0 is the header

    for (int i = 0; i < 10; ++i) {
        sidebar.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelDown));
    }
    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 1, 28).find("0.txt") == std::string::npos); // scrolled past it

    for (int i = 0; i < 20; ++i) {
        sidebar.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelUp));
    }
    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 1, 28).find("0.txt") != std::string::npos); // clamped back to the top

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // expand "sub/" (row 0 is the header)
    for (int i = 0; i < 5; ++i) {
        sidebar.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelDown));
    }

    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});
    sidebar.Paint(canvas);

    // "sub/" itself has scrolled out of the ordinary content area, but stays
    // pinned as a sticky header on the first content row (row 1 -- row 0 is
    // the project-name header) instead of disappearing.
    REQUIRE(RowText(screen, 1, 28).find("sub/") != std::string::npos);
    REQUIRE(screen.PixelAt(0, 1).foreground_color == theme.tabBar.foreground);

    std::filesystem::remove_all(dir);
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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});

    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 2, 28).find("nested") == std::string::npos); // collapsed by default

    sidebar.RevealPath(dir / "src" / "nested" / "file.txt");
    sidebar.Paint(canvas);

    // Row 0 is always the header. "src/" (row 1) and "nested/" (row 2) are
    // both now expanded, so "file.txt" (row 3) is directly visible without
    // any manual clicking.
    REQUIRE(RowText(screen, 1, 28).find("▾") != std::string::npos); // "src/" expanded
    REQUIRE(RowText(screen, 2, 28).find("nested") != std::string::npos);
    REQUIRE(RowText(screen, 2, 28).find("▾") != std::string::npos); // "nested/" expanded too
    REQUIRE(RowText(screen, 3, 28).find("file.txt") != std::string::npos);

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.RevealPath(dir / "file.txt"); // must not crash -- nothing to expand

    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});
    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 1, 28).find("file.txt") != std::string::npos); // row 0 is the header

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.RevealPath(std::filesystem::temp_directory_path() / "somewhere_else_entirely" / "file.txt");

    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});
    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 1, 28).find("▸") != std::string::npos); // "src/" still collapsed (row 0 is the header)

    std::filesystem::remove_all(dir);
}

TEST_CASE("A failed open reports an error via statusMessage without crashing", "[ProjectSidebar]") {
    // A real TOCTOU-style failure: the entry is listed fine (a regular file,
    // so it passes BuildProjectTree's own is_regular_file() check) but can't
    // actually be opened -- OpenOrCreateFile/Buffer::FromFile throws, and
    // ProjectSidebar::OnEvent must catch it rather than letting it propagate.
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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // must not crash (row 0 is the header)

    REQUIRE(&activeBuffer.Get() == &scratch);
    REQUIRE_FALSE(statusMessage.empty());

    std::filesystem::permissions(unreadable, std::filesystem::perms::owner_all, ec);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Clicking a binary file reports a message when no open-request handler is set", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_binary1";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream file(dir / "data.bin", std::ios::binary);
        file.put('a');
        file.put('\0');
    }

    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(0, 1)); // row 0 is the header

    REQUIRE(&activeBuffer.Get() == &scratch); // never opened
    REQUIRE(statusMessage.find("binary") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("While focused, arrow keys move the selection and Enter opens the file permanently, returning focus",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_kbd";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "aaa";
    }
    {
        std::ofstream(dir / "b.txt") << "bbb";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 6);

    bool focusReturned = false;
    sidebar.SetOnFocusReturn([&focusReturned] { focusReturned = true; });

    REQUIRE(sidebar.Focusable());
    sidebar.TakeFocus();
    REQUIRE(sidebar.Focused());

    // The accent-tinted header row while focused -- unified-left-dock
    // follow-up: the same "this has your attention" signal a border-accent
    // frame used to give, relocated to this widget's own header row now
    // that it no longer owns a frame.
    ned::ui::Screen screen = ned::ui::Screen(28, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 5});
    sidebar.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.borderAccent.foreground);
    // The selection cursor starts on the first entry (a.txt, row 1) and
    // washes the whole content row with the selection background.
    REQUIRE(screen.PixelAt(0, 1).background_color == theme.selectionBackground);
    REQUIRE_FALSE(screen.PixelAt(0, 2).background_color == theme.selectionBackground);

    sidebar.OnEvent(ned::ui::test::ArrowDown()); // a.txt -> b.txt
    sidebar.Paint(canvas);
    REQUIRE_FALSE(screen.PixelAt(0, 1).background_color == theme.selectionBackground);
    REQUIRE(screen.PixelAt(0, 2).background_color == theme.selectionBackground);

    sidebar.OnEvent(ned::ui::test::Return());
    REQUIRE(activeBuffer.Get().Text() == "bbb");
    REQUIRE(list.PreviewBuffer() == nullptr); // a deliberate keyboard open is permanent, never a preview
    REQUIRE(focusReturned);

    std::filesystem::remove_all(dir);
}

TEST_CASE("While focused, Right/Left expand/collapse a directory and Escape returns focus", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_kbd_dir";
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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 6);

    bool focusReturned = false;
    sidebar.SetOnFocusReturn([&focusReturned] { focusReturned = true; });
    sidebar.TakeFocus();

    ned::ui::Screen screen = ned::ui::Screen(28, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 5});

    sidebar.OnEvent(ned::ui::test::ArrowRight()); // expand "sub/"
    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 2, 28).find("nested.txt") != std::string::npos);

    sidebar.OnEvent(ned::ui::test::ArrowLeft()); // collapse it again
    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 2, 28).find("nested.txt") == std::string::npos);

    REQUIRE_FALSE(focusReturned);
    sidebar.OnEvent(ned::ui::test::Escape());
    REQUIRE(focusReturned);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Key events are ignored while the sidebar is not focused", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_kbd_unfocused";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 6);

    REQUIRE_FALSE(sidebar.OnEvent(ned::ui::test::Return())); // not consumed
    REQUIRE(&activeBuffer.Get() == &scratch);                // and nothing opened

    std::filesystem::remove_all(dir);
}

TEST_CASE("Collapsing the dock while the hosted sidebar is focused hands focus back via OnFocusPreempted",
          "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    ned::ui::LeftDock       dock(theme);
    dock.AddPanel(U'F', "Files", sidebar);

    bool focusReturned = false;
    sidebar.SetOnFocusReturn([&focusReturned] { focusReturned = true; });
    sidebar.TakeFocus();

    // unified-left-dock follow-up: collapse now lives on LeftDock, not
    // ProjectSidebar -- Widget::OnFocusPreempted is the bridge that still
    // lets a focused hosted panel hand focus back when the dock hides it.
    dock.SetCollapsed(true);
    REQUIRE(focusReturned);
}

TEST_CASE("Clicking a binary file hands off to the open-request handler when one is set", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_binary2";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream file(dir / "data.bin", std::ios::binary);
        file.put('a');
        file.put('\0');
    }

    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    std::optional<std::filesystem::path> requestedPath;
    sidebar.SetOnBinaryFileOpenRequest([&](const std::filesystem::path& path) { requestedPath = path; });

    sidebar.OnEvent(MousePress(0, 1));

    REQUIRE(&activeBuffer.Get() == &scratch); // handler is responsible for actually opening it, not this widget
    REQUIRE(requestedPath.has_value());
    REQUIRE(requestedPath->filename() == "data.bin");

    std::filesystem::remove_all(dir);
}

TEST_CASE("changed-files-highlight: rows are tinted by VCS status, directories by their most severe descendant",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_vcs_status";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    {
        std::ofstream(dir / "a.txt") << "x"; // modified
    }
    {
        std::ofstream(dir / "b.txt") << "x"; // untracked
    }
    {
        std::ofstream(dir / "c.txt") << "x"; // clean -- no status entry at all
    }
    {
        std::ofstream(dir / "sub" / "d.txt") << "x"; // added
    }
    {
        std::ofstream(dir / "sub" / "e.txt") << "x"; // modified -- outranks d.txt's Added when merged onto sub/
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 8);

    sidebar.OnEvent(MousePress(0, 1)); // expand "sub/" (row 1 -- row 0 is the header)
    sidebar.DispatchVcsStatusForTesting({
        {" M", "a.txt"},
        {"??", "b.txt"},
        {"A ", "sub/d.txt"},
        {" M", "sub/e.txt"},
    });

    ned::ui::Screen screen = ned::ui::Screen(28, 8);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 7});
    sidebar.Paint(canvas);

    // Visible rows in order: sub/ (row1), d.txt (row2), e.txt (row3),
    // a.txt (row4), b.txt (row5), c.txt (row6).
    REQUIRE(RowText(screen, 1, 28).find("sub/") != std::string::npos);
    REQUIRE(RowText(screen, 2, 28).find("d.txt") != std::string::npos);
    REQUIRE(RowText(screen, 3, 28).find("e.txt") != std::string::npos);
    REQUIRE(RowText(screen, 4, 28).find("a.txt") != std::string::npos);
    REQUIRE(RowText(screen, 5, 28).find("b.txt") != std::string::npos);
    REQUIRE(RowText(screen, 6, 28).find("c.txt") != std::string::npos);

    // sub/ has no status of its own but two changed descendants -- it's
    // colored by the more severe one (Modified beats Added).
    REQUIRE(screen.PixelAt(1, 1).foreground_color == ned::ui::Color::BrightBlue);
    REQUIRE(screen.PixelAt(1, 2).foreground_color == ned::ui::Color::BrightGreen); // d.txt: Added
    REQUIRE(screen.PixelAt(1, 3).foreground_color == ned::ui::Color::BrightBlue);  // e.txt: Modified
    REQUIRE(screen.PixelAt(1, 4).foreground_color == ned::ui::Color::BrightBlue);  // a.txt: Modified
    REQUIRE(screen.PixelAt(1, 5).foreground_color == ned::ui::Color::BrightCyan);  // b.txt: Untracked
    REQUIRE(screen.PixelAt(1, 6).foreground_color == theme.defaultForeground);     // c.txt: clean, untouched

    std::filesystem::remove_all(dir);
}

TEST_CASE("A right-press on a file entry reports its path/isDirectory and the click's absolute position, without opening it",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_context_menu_file";
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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    std::optional<std::filesystem::path> requestedPath;
    std::optional<bool>                  requestedIsDirectory;
    std::optional<ned::ui::Point>        requestedAnchor;
    sidebar.SetOnContextMenuRequest([&](const std::filesystem::path& path, bool isDirectory, ned::ui::Point anchor) {
        requestedPath        = path;
        requestedIsDirectory = isDirectory;
        requestedAnchor      = anchor;
    });

    sidebar.OnEvent(MousePress(3, 1, ned::ui::MouseEvent::Button::Right)); // row 0 is the header

    REQUIRE(requestedPath.has_value());
    REQUIRE(requestedPath->filename() == "target.txt");
    REQUIRE(requestedIsDirectory == false);
    REQUIRE(requestedAnchor == ned::ui::Point{.x = 3, .y = 1});
    // Unlike a left click, this never opens the file.
    REQUIRE(&activeBuffer.Get() == &scratch);

    std::filesystem::remove_all(dir);
}

TEST_CASE("A right-press on a directory entry reports isDirectory true, without toggling it", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_context_menu_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    std::optional<bool> requestedIsDirectory;
    sidebar.SetOnContextMenuRequest(
        [&](const std::filesystem::path&, bool isDirectory, ned::ui::Point) { requestedIsDirectory = isDirectory; });

    sidebar.OnEvent(MousePress(0, 1, ned::ui::MouseEvent::Button::Right)); // "sub/" (row 0 is the header)

    REQUIRE(requestedIsDirectory == true);
    // Unlike a left click, this never expands the directory.
    ned::ui::Screen screen = ned::ui::Screen(28, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 4});
    sidebar.Paint(canvas);
    REQUIRE(RowText(screen, 1, 28).find("▸") != std::string::npos); // still collapsed

    std::filesystem::remove_all(dir);
}

TEST_CASE("A left or right press anywhere in the widget takes keyboard focus", "[ProjectSidebar]") {
    // click-to-focus follow-up: reverses this widget's original "mouse-only,
    // clicking never steals keyboard focus" design.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_click_focus";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    // A wheel event never grabs focus -- scrolling to peek at the tree
    // shouldn't yank focus away from whatever you were typing.
    REQUIRE_FALSE(sidebar.Focused());
    sidebar.OnEvent(MouseWheel(1, 1, ned::ui::MouseEvent::Button::WheelDown));
    REQUIRE_FALSE(sidebar.Focused());

    sidebar.OnEvent(MousePress(1, 1)); // a tree row, left click
    REQUIRE(sidebar.Focused());

    std::filesystem::remove_all(dir);
}

TEST_CASE("A right-press over the header row reports the project root directory",
          "[ProjectSidebar]") {
    // project-root-context-menu follow-up: reversed from this test's own
    // prior "never fires" behavior -- a right-press on the header row now
    // reports the project root (isDirectory always true) instead of being
    // excluded as chrome, so main.cpp's context menu can offer New File/
    // New Folder with no existing entry needed to right-click first.
    // unified-left-dock follow-up: the divider column and bottom border row
    // this test used to also check are LeftDock's own chrome now, not this
    // widget's -- a right-press at either position is just ordinary tree
    // content here (or empty space past the tree, itself excluded only by
    // there being no entry to resolve, not by any reserved column/row).
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_context_menu_chrome";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    std::optional<std::filesystem::path> requestedPath;
    std::optional<bool>                  requestedIsDirectory;
    sidebar.SetOnContextMenuRequest([&](const std::filesystem::path& path, bool isDirectory, ned::ui::Point) {
        requestedPath        = path;
        requestedIsDirectory = isDirectory;
    });

    sidebar.OnEvent(MousePress(3, 0, ned::ui::MouseEvent::Button::Right)); // header row

    REQUIRE(requestedPath == ned::editor::ProjectRoot());
    REQUIRE(requestedIsDirectory == true);

    std::filesystem::remove_all(dir);
}

TEST_CASE("A right-press with no handler registered is a safe no-op", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_context_menu_nohandler";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "x";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    sidebar.OnEvent(MousePress(3, 1, ned::ui::MouseEvent::Button::Right)); // must not crash

    std::filesystem::remove_all(dir);
}
