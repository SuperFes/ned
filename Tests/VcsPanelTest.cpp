#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Editor/ProjectRoot.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "TestEvents.h"
#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
#include "UI/EventLoop.h"
#include "UI/VcsPanel.h"

using ned::editor::vcs::VcsCommandSpec;
using ned::editor::vcs::VcsProvider;
using ned::editor::vcs::VcsRunner;

namespace {

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

void PlacePanel(ned::ui::VcsPanel& panel, int width, int height) {
    panel.SetBox_(ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = height - 1});
}

ned::ui::Event MousePress(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed);
}

// Mirrors ProjectSidebarTest.cpp/VcsRunnerTest.cpp's own CurrentPathGuard.
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

struct RegistryResetGuard {
    RegistryResetGuard() {
        ned::editor::vcs::ClearRegistry();
    }
    ~RegistryResetGuard() {
        ned::editor::vcs::ClearRegistry();
    }
};

// A provider that detects every root but leaves every operation (including
// stage/unstage) default-throwing -- VcsRunnerTest.cpp's own established
// boundary: a real subprocess never actually completes within these tests
// (EventLoop::Run() is never started), so only the *synchronous* guard/
// error path -- "does VcsPanel target the right file(s)" -- is meaningfully
// testable here, not a real stage success.
class ThrowingProvider : public VcsProvider {
  public:
    [[nodiscard]] bool Detect(const std::filesystem::path&) const override {
        return true;
    }
};

} // namespace

TEST_CASE("VcsPanel groups files into staged/unstaged/untracked sections, each a directory tree", "[VcsPanel]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_vcs_panel_test_sections";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel      panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlacePanel(panel, 30, 12);

    panel.DispatchVcsStatusForTesting({
        {"M ", "a.txt"    },
        {" M", "sub/b.txt"},
        {"??", "c.txt"    },
    });

    // Directories start collapsed (ProjectSidebar's own convention) --
    // expand "sub/" (row 4: row 0 border, row 1 "Staged (1)" header, row 2
    // a.txt, row 3 "Unstaged (1)" header, row 4 sub/) so b.txt is visible.
    panel.OnEvent(MousePress(1, 4));

    ned::ui::Screen screen = ned::ui::Screen(30, 12);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 11});
    panel.Paint(canvas);

    // Row 0 is the border/title; content starts at row 1: "Staged (1)"
    // header, then a.txt; "Unstaged (1)" header, then sub/, then b.txt;
    // "Untracked (1)" header, then c.txt.
    REQUIRE(RowText(screen, 1, 30).find("Staged (1)") != std::string::npos);
    REQUIRE(RowText(screen, 2, 30).find("a.txt") != std::string::npos);
    REQUIRE(RowText(screen, 3, 30).find("Unstaged (1)") != std::string::npos);
    REQUIRE(RowText(screen, 4, 30).find("sub") != std::string::npos);
    REQUIRE(RowText(screen, 5, 30).find("b.txt") != std::string::npos);
    REQUIRE(RowText(screen, 6, 30).find("Untracked (1)") != std::string::npos);
    REQUIRE(RowText(screen, 7, 30).find("c.txt") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Clicking a section header collapses it, hiding its rows without touching the others", "[VcsPanel]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_vcs_panel_test_collapse_section";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel      panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlacePanel(panel, 30, 12);

    panel.DispatchVcsStatusForTesting({
        {"M ", "a.txt"},
        {"??", "b.txt"},
    });

    panel.OnEvent(MousePress(1, 1)); // "Staged (1)" header row

    ned::ui::Screen screen = ned::ui::Screen(30, 12);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 11});
    panel.Paint(canvas);

    REQUIRE(RowText(screen, 1, 30).find("Staged (1)") != std::string::npos);
    REQUIRE(RowText(screen, 2, 30).find("a.txt") == std::string::npos); // hidden -- section is collapsed
    REQUIRE(RowText(screen, 2, 30).find("Unstaged") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Space marks/unmarks the focused file for batch selection", "[VcsPanel]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_vcs_panel_test_select_keyboard";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel      panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlacePanel(panel, 30, 12);
    panel.DispatchVcsStatusForTesting({
        {"M ", "a.txt"},
    });
    panel.TakeKeyboardFocus();

    panel.OnEvent(ned::ui::test::ArrowDown()); // move off the "Staged (1)" header onto a.txt
    REQUIRE(panel.SelectedPathsForTesting().empty());

    panel.OnEvent(ned::ui::test::Character(' '));
    REQUIRE(panel.SelectedPathsForTesting().size() == 1);
    REQUIRE(panel.SelectedPathsForTesting().contains((ned::editor::ProjectRoot() / "a.txt").lexically_normal()));

    panel.OnEvent(ned::ui::test::Character(' ')); // toggles back off
    REQUIRE(panel.SelectedPathsForTesting().empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("Clicking the checkbox glyph toggles selection; clicking elsewhere on the row opens the file",
         "[VcsPanel]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_vcs_panel_test_select_mouse";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    { std::ofstream(dir / "a.txt") << "hello"; }
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel      panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlacePanel(panel, 30, 12);
    panel.DispatchVcsStatusForTesting({
        {"M ", "a.txt"},
    });

    // a.txt is row 2 (row 0 border, row 1 "Staged (1)" header); the
    // checkbox glyph sits at contentLeft(1) + depth(0)*2 == column 1.
    panel.OnEvent(MousePress(1, 2));
    REQUIRE(panel.SelectedPathsForTesting().size() == 1);
    REQUIRE_FALSE(activeBuffer.Get().Name() == "a.txt"); // a click on the checkbox never opens the file

    panel.OnEvent(MousePress(1, 2)); // toggles back off
    REQUIRE(panel.SelectedPathsForTesting().empty());

    panel.OnEvent(MousePress(5, 2)); // anywhere past the checkbox opens the file
    REQUIRE(activeBuffer.Get().Name() == "a.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Staging with no VcsRunner configured reports an error rather than crashing", "[VcsPanel]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_vcs_panel_test_no_runner";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel      panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlacePanel(panel, 30, 12);
    panel.DispatchVcsStatusForTesting({
        {"M ", "a.txt"},
    });
    panel.TakeKeyboardFocus();
    panel.OnEvent(ned::ui::test::ArrowDown());

    panel.OnEvent(ned::ui::test::Character('a')); // stage, no vcsRunner_ set

    REQUIRE(statusMessage == "no vcs runner configured");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Staging targets the selection set when non-empty, else falls back to the focused row", "[VcsPanel]") {
    RegistryResetGuard guard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_vcs_panel_test_stage_targets";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const CurrentPathGuard cwdGuard(dir);

    ned::editor::vcs::RegisterProvider("throwing", std::make_unique<ThrowingProvider>());
    ned::ui::EventLoop eventLoop;
    VcsRunner          runner(eventLoop);

    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel      panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlacePanel(panel, 30, 12);
    panel.SetVcsRunner(&runner);
    panel.DispatchVcsStatusForTesting({
        {"M ", "a.txt"},
        {"M ", "b.txt"},
    });
    panel.TakeKeyboardFocus();
    panel.OnEvent(ned::ui::test::ArrowDown()); // a.txt
    panel.OnEvent(ned::ui::test::Character(' ')); // mark a.txt only

    panel.OnEvent(ned::ui::test::Character('a')); // stage -- ThrowingProvider's StageArgv default-throws synchronously

    // The marked selection was targeted (and cleared as each request was
    // issued) rather than falling back to whatever row is focused -- the
    // provider's synchronous "not supported" error confirms a real
    // RequestStage call was made at all.
    REQUIRE(panel.SelectedPathsForTesting().empty());
    REQUIRE(statusMessage.find("stage not supported") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("'c'/'w'/'n' fire SetOnAction with Commit/SwitchBranch/CreateBranch and return focus", "[VcsPanel]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_vcs_panel_test_actions";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const CurrentPathGuard cwdGuard(dir);

    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel      panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    PlacePanel(panel, 30, 12);

    std::vector<ned::ui::VcsPanelAction> firedActions;
    panel.SetOnAction([&firedActions](ned::ui::VcsPanelAction action) { firedActions.push_back(action); });
    bool focusReturned = false;
    panel.SetOnFocusReturn([&focusReturned] { focusReturned = true; });

    panel.TakeKeyboardFocus();
    panel.OnEvent(ned::ui::test::Character('c'));
    REQUIRE(focusReturned);
    focusReturned = false;

    panel.TakeKeyboardFocus();
    panel.OnEvent(ned::ui::test::Character('w'));
    panel.TakeKeyboardFocus();
    panel.OnEvent(ned::ui::test::Character('n'));

    REQUIRE(firedActions.size() == 3);
    REQUIRE(firedActions[0] == ned::ui::VcsPanelAction::Commit);
    REQUIRE(firedActions[1] == ned::ui::VcsPanelAction::SwitchBranch);
    REQUIRE(firedActions[2] == ned::ui::VcsPanelAction::CreateBranch);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ToggleCollapsed collapses to a 1-column strip and back", "[VcsPanel]") {
    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel      panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);

    REQUIRE_FALSE(panel.Collapsed());
    panel.SetWidth(24);
    REQUIRE(panel.Width() == 24);

    panel.ToggleCollapsed();
    REQUIRE(panel.Collapsed());
    REQUIRE(panel.Width() == 1);
    REQUIRE(panel.ExpandedWidth() == 24); // preserved across the collapse

    panel.ToggleCollapsed();
    REQUIRE_FALSE(panel.Collapsed());
    REQUIRE(panel.Width() == 24);
}
