#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "Editor/ProjectRoot.h"
#include "TestEvents.h"
#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
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

    // Row 0 is always the project-name header (sidebar-header follow-up) --
    // tree content starts at row 1. dir's own filename is longer than the
    // 27-column content width, so it renders truncated -- checking a
    // guaranteed-to-fit prefix rather than the full name.
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
    // content starts at column 1 now, inside the frame (chrome redesign).
    const std::string firstChar = screen.PixelAt(1, 1).character;
    REQUIRE((firstChar == "│" || firstChar == "└" || firstChar == "├"));
    // The rounded frame itself: corners plus the title embedded in the top
    // edge, and the right border doubling as the divider.
    REQUIRE(screen.PixelAt(0, 0).character == "╭");
    REQUIRE(screen.PixelAt(27, 0).character == "╮");
    REQUIRE(screen.PixelAt(0, 4).character == "╰");
    REQUIRE(screen.PixelAt(27, 4).character == "╯");
    REQUIRE(screen.PixelAt(0, 2).character == "│");
    REQUIRE(screen.PixelAt(27, 1).character == "│"); // plain right border (the tab-underline ├ junction left with that row)

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
    // starts at column 1, inside the frame.
    REQUIRE(RowText(screen, 1, 28).find("a.txt") != std::string::npos);
    REQUIRE(screen.PixelAt(1, 1).foreground_color == theme.activeTab.foreground);
    REQUIRE(RowText(screen, 2, 28).find("b.txt") != std::string::npos);
    REQUIRE_FALSE(screen.PixelAt(1, 2).foreground_color == theme.activeTab.foreground);

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
    REQUIRE(screen.PixelAt(1, 1).foreground_color == theme.tabBar.foreground);

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
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 5);

    REQUIRE_FALSE(sidebar.IsResizing());
    sidebar.OnEvent(MousePress(27, 0)); // divider column
    REQUIRE(sidebar.IsResizing());
    REQUIRE(&activeBuffer.Get() == &scratch); // did not open "only.txt"

    sidebar.OnEvent(MouseRelease(27, 0));
    REQUIRE_FALSE(sidebar.IsResizing());

    std::filesystem::remove_all(dir);
}

TEST_CASE("Shrinking the divider updates Width(), anchored to the drag's total displacement", "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 20, 5);

    sidebar.OnEvent(MousePress(19, 0)); // divider column
    REQUIRE(sidebar.IsResizing());

    sidebar.OnEvent(MouseMove(10, 0)); // dragged 9 columns left of the press point
    REQUIRE(sidebar.Width() == 11);

    // A second move is measured from the *original* press, not the previous
    // move -- dragging back out to x=19 (0 net displacement) restores the
    // starting width exactly, not some compounded value.
    sidebar.OnEvent(MouseMove(19, 0));
    REQUIRE(sidebar.Width() == 20);
}

TEST_CASE("Dragging the divider clamps to a minimum width", "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 20, 5);

    sidebar.OnEvent(MousePress(19, 0));
    sidebar.OnEvent(MouseMove(-1000, 0)); // absurdly far left

    REQUIRE(sidebar.Width() > 0); // clamped, not driven negative or to zero
}

TEST_CASE("A move without a resize in progress is a safe no-op", "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 20, 5);
    const int before = sidebar.Width();

    sidebar.OnEvent(MouseMove(5, 0)); // must not crash or change anything

    REQUIRE(sidebar.Width() == before);
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

TEST_CASE("The frame renders with the border brush, switching to the accent brush during a resize drag",
          "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 20, 5);

    ned::ui::Screen screen = ned::ui::Screen(20, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});

    sidebar.Paint(canvas);
    REQUIRE(screen.PixelAt(19, 2).foreground_color == theme.border.foreground);
    // The title text takes the accent brush.
    REQUIRE(screen.PixelAt(3, 0).foreground_color == theme.borderAccent.foreground);

    sidebar.OnEvent(MousePress(19, 2)); // start a resize on the divider
    REQUIRE(sidebar.IsResizing());
    sidebar.Paint(canvas);
    REQUIRE(screen.PixelAt(19, 2).foreground_color == theme.borderAccent.foreground);

    sidebar.OnEvent(MouseRelease(19, 2));
    sidebar.Paint(canvas);
    REQUIRE(screen.PixelAt(19, 2).foreground_color == theme.border.foreground);
}

TEST_CASE("Double-clicking the divider collapses to a 1-column strip; double-clicking the strip expands again",
          "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 20, 5);
    REQUIRE_FALSE(sidebar.Collapsed());
    REQUIRE(sidebar.Width() == 30); // the default expanded width

    sidebar.OnEvent(MousePress(19, 2)); // first press starts a resize...
    REQUIRE(sidebar.IsResizing());
    sidebar.OnEvent(MousePress(19, 2)); // ...the rapid second press collapses instead

    REQUIRE(sidebar.Collapsed());
    REQUIRE_FALSE(sidebar.IsResizing()); // the half-started resize died with the frame
    REQUIRE(sidebar.Width() == 1);
    REQUIRE(sidebar.ExpandedWidth() == 30); // preserved for re-expansion

    // The collapsed strip: border line with an accent hint glyph on top.
    sidebar.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(1, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 4});
    sidebar.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).character == "▸");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.borderAccent.foreground);
    REQUIRE(screen.PixelAt(0, 2).character == "│");
    REQUIRE(screen.PixelAt(0, 2).foreground_color == theme.border.foreground);

    // A single press on the strip does nothing; a rapid second one expands.
    sidebar.OnEvent(MousePress(0, 2));
    REQUIRE(sidebar.Collapsed());
    sidebar.OnEvent(MousePress(0, 2));
    REQUIRE_FALSE(sidebar.Collapsed());
    REQUIRE(sidebar.Width() == 30);
}

TEST_CASE("A real resize drag never counts as the first half of a collapse double-click", "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 20, 5);

    sidebar.OnEvent(MousePress(19, 2));
    sidebar.OnEvent(MouseMove(12, 2)); // a genuine drag, well past the wobble allowance
    sidebar.OnEvent(MouseRelease(12, 2));
    REQUIRE(sidebar.Width() == 13);

    // The composition root re-reads Width() and re-lays the box out every
    // frame -- mirror that before pressing the (moved) divider again.
    PlaceSidebar(sidebar, 13, 5);

    // A prompt new press on the divider (well within the double-click
    // window of the drag's own initial press) must start a fresh resize,
    // not collapse.
    sidebar.OnEvent(MousePress(12, 2));
    REQUIRE_FALSE(sidebar.Collapsed());
    REQUIRE(sidebar.IsResizing());
    sidebar.OnEvent(MouseRelease(12, 2));
}

TEST_CASE("ToggleCollapsed drives the same collapse the divider double-click does (C-c C-p's path)",
          "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);

    sidebar.SetWidth(42);
    sidebar.ToggleCollapsed();
    REQUIRE(sidebar.Collapsed());
    REQUIRE(sidebar.Width() == 1);
    REQUIRE(sidebar.ExpandedWidth() == 42);
    sidebar.ToggleCollapsed();
    REQUIRE_FALSE(sidebar.Collapsed());
    REQUIRE(sidebar.Width() == 42);
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

    // The accent frame while focused -- the same signal a resize drag gives.
    ned::ui::Screen screen = ned::ui::Screen(28, 6);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 5});
    sidebar.Paint(canvas);
    REQUIRE(screen.PixelAt(27, 2).foreground_color == theme.borderAccent.foreground);
    // The selection cursor starts on the first entry (a.txt, row 1) and
    // washes the whole content row with the selection background.
    REQUIRE(screen.PixelAt(1, 1).background_color == theme.selectionBackground);
    REQUIRE_FALSE(screen.PixelAt(1, 2).background_color == theme.selectionBackground);

    sidebar.OnEvent(ned::ui::test::ArrowDown()); // a.txt -> b.txt
    sidebar.Paint(canvas);
    REQUIRE_FALSE(screen.PixelAt(1, 1).background_color == theme.selectionBackground);
    REQUIRE(screen.PixelAt(1, 2).background_color == theme.selectionBackground);

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

TEST_CASE("Collapsing a focused sidebar hands focus back instead of capturing the keyboard in a strip",
          "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);

    bool focusReturned = false;
    sidebar.SetOnFocusReturn([&focusReturned] { focusReturned = true; });
    sidebar.TakeFocus();

    sidebar.SetCollapsed(true);
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

TEST_CASE("TakeKeyboardFocus expands a collapsed sidebar and returning focus re-collapses it", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_kbd_recollapse";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "aaa";
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

    sidebar.SetCollapsed(true);
    sidebar.TakeKeyboardFocus();
    REQUIRE(sidebar.Focused());
    REQUIRE_FALSE(sidebar.Collapsed()); // focus into a 1-column strip would be meaningless

    sidebar.OnEvent(ned::ui::test::Escape());
    REQUIRE(focusReturned);
    REQUIRE(sidebar.Collapsed()); // summoned hidden -> goes back to hidden

    std::filesystem::remove_all(dir);
}

TEST_CASE("TakeKeyboardFocus on an already-expanded sidebar leaves it expanded when focus returns",
          "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_kbd_expanded";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "aaa";
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

    sidebar.TakeKeyboardFocus();
    sidebar.OnEvent(ned::ui::test::Escape());
    REQUIRE(focusReturned);
    REQUIRE_FALSE(sidebar.Collapsed());

    std::filesystem::remove_all(dir);
}

TEST_CASE("Enter opening a file from a keyboard-summoned collapsed sidebar re-collapses it too", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_kbd_open_recollapse";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "aaa";
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

    sidebar.SetCollapsed(true);
    sidebar.TakeKeyboardFocus();
    sidebar.OnEvent(ned::ui::test::Return()); // opens a.txt (the first entry) and returns focus

    REQUIRE(activeBuffer.Get().Text() == "aaa");
    REQUIRE(focusReturned);
    REQUIRE(sidebar.Collapsed());

    std::filesystem::remove_all(dir);
}

TEST_CASE("A divider drag that moved commits its width once on release; a no-move click commits nothing",
          "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 20, 5);

    std::optional<int> committedWidth;
    int                commitCount = 0;
    sidebar.SetOnWidthCommitted([&](int width) {
        committedWidth = width;
        ++commitCount;
    });

    // A press/release on the divider with no movement -- nothing to remember.
    sidebar.OnEvent(MousePress(19, 0));
    sidebar.OnEvent(MouseRelease(19, 0));
    REQUIRE_FALSE(committedWidth.has_value());

    // Wait out the double-click window so the next press starts a fresh
    // resize instead of collapsing.
    std::this_thread::sleep_for(std::chrono::milliseconds(450));

    sidebar.OnEvent(MousePress(19, 0));
    sidebar.OnEvent(MouseMove(12, 0));         // dragged 7 columns left
    REQUIRE_FALSE(committedWidth.has_value()); // mid-drag: not committed yet
    sidebar.OnEvent(MouseRelease(12, 0));

    REQUIRE(committedWidth == 13);
    REQUIRE(commitCount == 1);
    REQUIRE(sidebar.Width() == 13);
}

TEST_CASE("Deliberate collapse toggles commit through the hook; programmatic changes don't", "[ProjectSidebar]") {
    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 20, 5);

    std::vector<bool> committed;
    sidebar.SetOnCollapseCommitted([&](bool collapsed) { committed.push_back(collapsed); });

    // Programmatic changes (session restore, remembered-variable startup
    // application) must not rewrite the remembered preference.
    sidebar.SetCollapsed(true);
    sidebar.SetCollapsed(false);
    REQUIRE(committed.empty());

    sidebar.ToggleCollapsed(); // toggle-project-sidebar's path
    sidebar.ToggleCollapsed();
    REQUIRE(committed == std::vector<bool>{true, false});

    // The divider double-click commits through the same helper.
    sidebar.OnEvent(MousePress(19, 2));
    sidebar.OnEvent(MousePress(19, 2));
    REQUIRE(committed == std::vector<bool>{true, false, true});
}

TEST_CASE("A keyboard summon of a hidden sidebar never commits visibility", "[ProjectSidebar]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_sidebar_test_kbd_no_commit";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "aaa";
    }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList   list;
    ned::text::Buffer&      scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer   activeBuffer(scratch);
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ProjectSidebar sidebar([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlaceSidebar(sidebar, 28, 6);

    std::vector<bool> committed;
    sidebar.SetOnCollapseCommitted([&](bool collapsed) { committed.push_back(collapsed); });

    sidebar.SetCollapsed(true);
    sidebar.TakeKeyboardFocus();              // transient expand
    sidebar.OnEvent(ned::ui::test::Escape()); // and its restore
    REQUIRE(sidebar.Collapsed());
    REQUIRE(committed.empty()); // a quick C-c p jump leaves the remembered preference alone

    std::filesystem::remove_all(dir);
}
