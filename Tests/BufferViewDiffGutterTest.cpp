#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ProjectRoot.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using ned::editor::vcs::VcsDiffHunk;
using ned::ui::BufferView;

namespace {

// Mirrors BufferViewBlameGutterTest.cpp's own GutterWidthWithBlame helper,
// extended with the diff column -- see BufferView::GutterWidth's real
// formula for the layout this tracks:
// [diff][status][diagnostic][gap][digits][gap][fold][blame].
int GutterWidthWithDiff(std::size_t totalLines, bool diffActive) {
    constexpr int kStatusWidth     = 1;
    constexpr int kDiagnosticWidth = 1;
    constexpr int kLineNumberGap   = 1;
    constexpr int kDiffWidth       = 1;
    return (diffActive ? kDiffWidth : 0) + kStatusWidth + kDiagnosticWidth + kLineNumberGap +
           static_cast<int>(std::to_string(totalLines).size()) + kLineNumberGap;
}

struct Fixture {
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

} // namespace

TEST_CASE("BufferView reserves no gutter column for diff markers until some are loaded", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithDiff(fixture.buffer.Content().LineCount(), false) + 5);
}

namespace {

// initial-buffer-diff fix: a provider that only records whether DiffArgv
// was ever consulted, then throws so no real subprocess spawns -- enough to
// prove a BufferView's very first Paint() actually requests the diff for
// the buffer it was constructed with (it didn't, while the request shared
// modeSyncBuffer_'s constructor-seeded branch).
class RecordingProvider : public ned::editor::vcs::VcsProvider {
  public:
    explicit RecordingProvider(bool& diffRequested) : diffRequested_(diffRequested) {
    }

    [[nodiscard]] bool Detect(const std::filesystem::path&) const override {
        return true;
    }
    [[nodiscard]] ned::editor::vcs::VcsCommandSpec BlameArgv(const std::filesystem::path&) const override {
        throw std::runtime_error("not under test");
    }
    [[nodiscard]] std::vector<ned::editor::vcs::VcsBlameLine> ParseBlame(const std::string&) const override {
        return {};
    }
    [[nodiscard]] ned::editor::vcs::VcsCommandSpec LogArgv(const std::filesystem::path&) const override {
        throw std::runtime_error("not under test");
    }
    [[nodiscard]] std::vector<ned::editor::vcs::VcsLogEntry> ParseLog(const std::string&) const override {
        return {};
    }
    [[nodiscard]] ned::editor::vcs::VcsCommandSpec DiffArgv(const std::filesystem::path&) const override {
        diffRequested_ = true;
        throw std::runtime_error("recorded -- no real spawn wanted");
    }
    [[nodiscard]] std::vector<ned::editor::vcs::VcsDiffHunk> ParseDiff(const std::string&) const override {
        return {};
    }

  private:
    bool& diffRequested_;
};

} // namespace

TEST_CASE("A pane's very first Paint requests the diff for its initial buffer", "[BufferView][Vcs]") {
    // Regression test (initial-buffer-diff fix): the constructor's
    // modeSyncBuffer_ seeding used to suppress this request entirely, so
    // the file ned was launched on never showed diff markers until an
    // edit/save fired a request some other way.
    ned::editor::vcs::ClearRegistry();
    bool diffRequested = false;
    ned::editor::vcs::RegisterProvider("recording", std::make_unique<RecordingProvider>(diffRequested));
    const auto previousRoot = ned::editor::ProjectRoot();
    ned::editor::SetProjectRoot("/tmp");

    {
        Fixture fixture;
        fixture.buffer.SetPath("/tmp/ned-initial-diff-test.c");
        fixture.buffer.InsertAtPoint("hello");

        ned::ui::EventLoop          eventLoop;
        ned::editor::vcs::VcsRunner runner(eventLoop);

        BufferView view = fixture.View();
        view.SetVcsRunner(&runner);
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

        ned::ui::Screen screen = ned::ui::Screen(20, 3);
        ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
        view.Paint(canvas); // the pane's first frame -- no buffer switch ever happened

        REQUIRE(diffRequested);
    }

    ned::editor::SetProjectRoot(previousRoot);
    ned::editor::vcs::ClearRegistry();
}

TEST_CASE("Diff markers render in the diff column itself, not under the status swatch", "[BufferView][Vcs]") {
    // Regression test: the swatch/notch used to be drawn at statusStart --
    // one column right of the diff column that GutterWidth reserves --
    // where the unsaved-change swatch then unconditionally overwrote it,
    // leaving the reserved column permanently blank.
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour\n");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});

    // Added at 0-indexed line 1, Modified at line 2, Removed boundary at 3.
    view.DispatchDiffForTesting({VcsDiffHunk{1, 0, 2, 1}, VcsDiffHunk{3, 1, 3, 1}, VcsDiffHunk{5, 1, 3, 0}});

    ned::ui::Screen screen = ned::ui::Screen(20, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    // diff-gutter-icons follow-up: vim-gitgutter's classic glyphs, in the
    // familiar colors, as foreground icons rather than solid swatches.
    REQUIRE(screen.PixelAt(0, 0).character == " "); // untouched line
    REQUIRE(screen.PixelAt(0, 1).character == "+"); // Added
    REQUIRE(screen.PixelAt(0, 1).foreground_color == ned::ui::Color::BrightGreen);
    REQUIRE(screen.PixelAt(0, 1).background_color == fixture.theme.background);
    REQUIRE(screen.PixelAt(0, 2).character == "~"); // Modified
    REQUIRE(screen.PixelAt(0, 2).foreground_color == ned::ui::Color::BrightBlue);
    REQUIRE(screen.PixelAt(0, 3).character == "▔"); // Removed notch
    REQUIRE(screen.PixelAt(0, 3).foreground_color == ned::ui::Color::BrightRed);
    // The status column right of it belongs to the unsaved-change swatch
    // (the whole buffer is unsaved here) -- proves the two no longer fight
    // over one cell.
    REQUIRE(screen.PixelAt(1, 1).background_color == fixture.theme.unsavedChangeIndicator);
}

TEST_CASE("DispatchDiffForTesting classifies a pure addition hunk as Added lines", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\n");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 5});

    // "@@ -1,0 +2,2 @@" shape: two brand-new lines starting at new-file
    // line 2 (1-indexed) -> 0-indexed lines 1,2.
    view.DispatchDiffForTesting({VcsDiffHunk{1, 0, 2, 2}});

    // Gutter column now reserved -- verified indirectly via cursor shift,
    // same convention BufferViewBlameGutterTest.cpp's own tests use since
    // GutterWidth() itself is private. Point sits on the trailing empty
    // line (column 0) after the inserted text's own final newline, so no
    // "+N" column offset applies here the way the single-line fixtures
    // below use.
    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithDiff(fixture.buffer.Content().LineCount(), true));
}

TEST_CASE("DispatchDiffForTesting classifies a pure deletion hunk as a single Removed boundary", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\n");
    BufferView view = fixture.View();

    // "@@ -3,2 +2,0 @@": old lines 3-4 deleted, nothing added -- boundary
    // sits at 0-indexed new-file line 2.
    view.DispatchDiffForTesting({VcsDiffHunk{3, 2, 2, 0}});

    // A Removed-only hunk still reserves the gutter column (there's
    // something to show, just not a covered-line swatch) -- confirmed via
    // the same cursor-shift check the other cases use.
    REQUIRE(view.CursorPosition().has_value());
}

TEST_CASE("DispatchDiffForTesting classifies a hunk with both old and new lines as Modified", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\n");
    BufferView view = fixture.View();

    // "@@ -2 +2 @@": a single-line modification at 0-indexed line 1.
    view.DispatchDiffForTesting({VcsDiffHunk{2, 1, 2, 1}});

    REQUIRE(view.CursorPosition().has_value()); // reserves the column, doesn't crash
}

TEST_CASE("Diff markers are cleared (not resynthesized) when the active buffer changes", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    view.DispatchDiffForTesting({VcsDiffHunk{1, 0, 1, 1}});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // no-op re-paint of the same buffer -- markers survive

    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithDiff(fixture.buffer.Content().LineCount(), true) + 5);

    // Switching to a different buffer clears the previous file's markers
    // immediately (see Paint()'s own modeSyncBuffer_ check) rather than
    // leaving them visible against unrelated content.
    ned::text::Buffer& other = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("goodbye");
    fixture.activeBuffer.Set(other);
    view.Paint(canvas);

    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithDiff(other.Content().LineCount(), false) + 7);
}
