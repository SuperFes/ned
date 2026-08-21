#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "TestEvents.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::vcs::VcsBlameLine;
using ned::ui::BufferView;

namespace {

// Mirrors BufferView::GutterWidth's formula, extended with the blame
// gutter's own rightmost column -- see BufferViewTest.cpp's own identically-
// named/purposed helper for the pre-blame version this is based on.
int GutterWidthWithBlame(std::size_t totalLines, bool blameActive) {
    constexpr int kStatusWidth     = 1;
    constexpr int kDiagnosticWidth = 1;
    constexpr int kLineNumberGap   = 1;
    constexpr int kBlameWidth      = 9;
    return kStatusWidth + kDiagnosticWidth + kLineNumberGap + static_cast<int>(std::to_string(totalLines).size()) +
           kLineNumberGap + (blameActive ? kBlameWidth : 0);
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

std::vector<VcsBlameLine> OneLineOfBlame() {
    return {VcsBlameLine{"abcdef1234567890abcdef1234567890abcdef12", "Ada", "2026-01-01", "did a thing"}};
}

} // namespace

TEST_CASE("BufferView reserves no gutter column for blame until it's been populated", "[BufferView][Vcs]") {
    Fixture    fixture;
    fixture.buffer.InsertAtPoint("hello");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    REQUIRE_FALSE(view.BlameGutterActive());
    // GutterWidth() itself is private (BufferViewTest.cpp's own convention
    // is to mirror its formula rather than call it directly) -- exercised
    // indirectly here via where the cursor actually lands on screen, the
    // same "content starts right after the gutter" fact CursorPosition
    // itself depends on.
    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithBlame(fixture.buffer.Content().LineCount(), false) + 5);
}

TEST_CASE("DispatchBlameForTesting populates the blame gutter and shifts content over by kBlameWidth", "[BufferView][Vcs]") {
    Fixture    fixture;
    fixture.buffer.InsertAtPoint("hello");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    view.DispatchBlameForTesting(OneLineOfBlame());

    REQUIRE(view.BlameGutterActive());
    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x == GutterWidthWithBlame(fixture.buffer.Content().LineCount(), true) + 5);
}

TEST_CASE("Editing the buffer after blame is populated goes stale and clears on the next Paint()", "[BufferView][Vcs]") {
    Fixture    fixture;
    BufferView view = fixture.View();

    view.DispatchBlameForTesting(OneLineOfBlame());
    REQUIRE(view.BlameGutterActive());

    fixture.buffer.InsertAtPoint("x"); // bumps ContentGeneration()

    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(20, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    REQUIRE_FALSE(view.BlameGutterActive()); // stale -- cleared, not resynthesized, by Paint()'s EnsureBlameGutterCache call
}

TEST_CASE("vcs-blame-detail-at-point (C-c v i) reports the full commit info for the blamed line at point",
          "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("some code");
    fixture.buffer.SetPoint(0);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.DispatchBlameForTesting(OneLineOfBlame());

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("v"));
    view.OnEvent(ned::ui::test::Character("i"));

    REQUIRE(fixture.statusMessage.find("abcdef1234567890abcdef1234567890abcdef12") == 0);
    REQUIRE(fixture.statusMessage.find("Ada") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("did a thing") != std::string::npos);
}

TEST_CASE("vcs-blame-detail-at-point reports a clear message when no blame is loaded", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("some code");
    fixture.buffer.SetPoint(0);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("v"));
    view.OnEvent(ned::ui::test::Character("i"));

    REQUIRE(fixture.statusMessage == "no blame data loaded -- run vcs-show-blame (C-c v b) first");
}

TEST_CASE("RequestBlameForCurrentBuffer with no VcsRunner configured reports a status message, not a crash",
          "[BufferView][Vcs]") {
    Fixture    fixture;
    BufferView view = fixture.View();

    view.RequestBlameForCurrentBuffer();

    REQUIRE(fixture.statusMessage == "no vcs runner configured");
    REQUIRE_FALSE(view.BlameGutterActive());
}

TEST_CASE("vcs-show-blame (C-c v b) stays on the current buffer rather than switching to a results buffer",
          "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("some code");

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("v"));
    view.OnEvent(ned::ui::test::Character("b"));

    // No VcsRunner configured in this fixture -- reports the error inline,
    // via RequestBlameForCurrentBuffer, and (unlike the old default) never
    // touches which buffer is active.
    REQUIRE(fixture.statusMessage == "no vcs runner configured");
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
}

TEST_CASE("vcs-show-blame (C-c v b) toggles off when blame is already showing for the current buffer",
          "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("some code");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.DispatchBlameForTesting(OneLineOfBlame());
    REQUIRE(view.BlameGutterActive());

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("v"));
    view.OnEvent(ned::ui::test::Character("b"));

    REQUIRE_FALSE(view.BlameGutterActive());
    REQUIRE(fixture.statusMessage == "blame hidden");
    // No VcsRunner configured -- if this had fallen through to a fresh
    // fetch instead of toggling off, it would have overwritten the status
    // message with "no vcs runner configured" instead.
}

TEST_CASE("vcs-visit-result (C-c v v) jumps from a synthesized *vcs blame*-shaped line to the real file/line",
          "[BufferView][Vcs]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_vcs_visit_result";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path targetFile = dir / "blamed.txt";
    { std::ofstream(targetFile) << "line one\nline two\nline three\n"; }

    Fixture fixture;
    // Same shape BuildVcsBlameBuffer itself writes: "<path>:<line>: <hash>
    // <author> <date> | <summary>" -- built by hand here rather than via
    // RequestVcsBlameBuffer/VcsRunner, which need a real, running EventLoop
    // to ever complete (this codebase's established "never run one in a
    // unit test" convention -- see TaskProcessTest.cpp/TaskRunnerTest.cpp).
    // This exercises the actual jump-back parsing/logic exhaustively
    // without needing that.
    ned::text::Buffer& blameBuffer = fixture.bufferList.CreateBuffer("*vcs blame blamed.txt*");
    blameBuffer.InsertAtPoint(targetFile.string() + ":2: abcd1234 Ada 2026-01-01 | did a thing\n");
    blameBuffer.SetPoint(0);
    blameBuffer.SetReadOnly(true);
    fixture.activeBuffer.Set(blameBuffer);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("v"));
    view.OnEvent(ned::ui::test::Character("v"));

    REQUIRE(fixture.activeBuffer.Get().Name() == "blamed.txt");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 1); // 0-indexed line 1 == "line two"

    std::filesystem::remove_all(dir);
}

TEST_CASE("vcs-visit-result is a silent no-op on a non-matching line", "[BufferView][Vcs]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("just some ordinary text, not a blame line");
    fixture.buffer.SetPoint(0);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("v"));
    view.OnEvent(ned::ui::test::Character("v"));

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer); // unchanged
}
