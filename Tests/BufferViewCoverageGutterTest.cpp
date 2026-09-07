#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Coverage/CoverageConfig.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::coverage::ClearCoverageReport;
using ned::editor::coverage::LoadCoverageReport;
using ned::editor::coverage::SetCoverageFile;
using ned::editor::vcs::VcsDiffHunk;
using ned::ui::BufferView;
using ned::ui::Color;

namespace {

// [diff][status][diagnostic][gap][digits][gap][test][coverage] --
// BufferViewTestGutterTest's own TestColumnX helper, renamed: the coverage
// column sits exactly where the (absent, no TestRunner wired) test column
// would, since coverageStart is computed as testStart + testColumnWidth
// regardless of whether test discovery is active (BufferView.cpp's own
// Paint()/GutterWidth() layout). diffActive accounts for the one-column
// diff gutter that DispatchDiffForTesting reserves (leftmost of all, ahead
// of status) once any hunk is dispatched -- omitted by every test here
// except the one that actually dispatches a hunk.
int CoverageColumnX(std::size_t totalLines, bool diffActive = false) {
    constexpr int kDiffWidth       = 1;
    constexpr int kStatusWidth     = 1;
    constexpr int kDiagnosticWidth = 1;
    constexpr int kLineNumberGap   = 1;
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
    ned::editor::Mode            mode  = ned::editor::PythonMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

struct ConfigResetGuard {
    ~ConfigResetGuard() {
        SetCoverageFile("");
        ClearCoverageReport();
    }
};

void PaintInto(BufferView& view, ned::ui::Screen& screen) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 4});
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 4});
    view.Paint(canvas);
}

void WriteInfoFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

} // namespace

TEST_CASE("Coverage gutter reserves nothing without a loaded report", "[BufferView][Coverage]") {
    ConfigResetGuard guard;
    Fixture          fixture;
    fixture.buffer.InsertAtPoint("a = 1\n");
    fixture.buffer.SetPath("/tmp/coverage-gutter-test-unloaded.py");

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 4});
    REQUIRE(view.CursorPosition().has_value());
    const int withoutCoverageColumn = view.CursorPosition()->x;

    const std::string path = "/tmp/coverage-gutter-test-unmatched.info";
    WriteInfoFile(path, "SF:/tmp/some-other-file.py\nDA:1,1\nend_of_record\n");
    SetCoverageFile(path);
    LoadCoverageReport(); // a real report, but for a different file -- still nothing to show here
    REQUIRE(view.CursorPosition()->x == withoutCoverageColumn);
}

TEST_CASE("Coverage gutter marks covered/partial/uncovered lines after a loaded report", "[BufferView][Coverage]") {
    ConfigResetGuard  guard;
    Fixture           fixture;
    const std::string bufferPath = "/tmp/coverage-gutter-test-sample.py";
    fixture.buffer.InsertAtPoint("a = 1\n"
                                 "b = 2\n"
                                 "c = 3\n");
    fixture.buffer.SetPath(bufferPath);

    const std::string infoPath = "/tmp/coverage-gutter-test-sample.info";
    WriteInfoFile(infoPath, "SF:" + bufferPath +
                                "\n"
                                "DA:1,5\n"
                                "DA:2,0\n"
                                "DA:3,5\n"
                                "BRDA:3,0,0,5\n"
                                "BRDA:3,0,1,0\n"
                                "end_of_record\n");
    SetCoverageFile(infoPath);
    LoadCoverageReport();

    BufferView      view = fixture.View();
    ned::ui::Screen screen(60, 5);
    PaintInto(view, screen);

    const int x = CoverageColumnX(fixture.buffer.Content().LineCount());
    CHECK(screen.PixelAt(x, 0).background_color == Color::Green);        // covered
    CHECK(screen.PixelAt(x, 1).background_color == Color::BrightRed);    // uncovered
    CHECK(screen.PixelAt(x, 2).background_color == Color::BrightYellow); // partial (one branch never taken)
}

TEST_CASE("Coverage gutter flags an uncovered line that's also newly changed", "[BufferView][Coverage]") {
    ConfigResetGuard  guard;
    Fixture           fixture;
    const std::string bufferPath = "/tmp/coverage-gutter-test-changed.py";
    fixture.buffer.InsertAtPoint("a = 1\n"
                                 "b = 2\n");
    fixture.buffer.SetPath(bufferPath);

    const std::string infoPath = "/tmp/coverage-gutter-test-changed.info";
    WriteInfoFile(infoPath, "SF:" + bufferPath + "\nDA:1,5\nDA:2,0\nend_of_record\n");
    SetCoverageFile(infoPath);
    LoadCoverageReport();

    BufferView view = fixture.View();
    // Line 2 (1-indexed) is a fresh addition -- oldCount == 0.
    view.DispatchDiffForTesting({VcsDiffHunk{.oldStart = 1, .oldCount = 0, .newStart = 2, .newCount = 1}});

    ned::ui::Screen screen(60, 5);
    PaintInto(view, screen);

    const int x = CoverageColumnX(fixture.buffer.Content().LineCount(), /*diffActive=*/true);
    CHECK(screen.PixelAt(x, 1).character == "!");
    CHECK(screen.PixelAt(x, 1).foreground_color == Color::BrightRed);
}

TEST_CASE("Coverage gutter cache refreshes when the report is reloaded", "[BufferView][Coverage]") {
    ConfigResetGuard  guard;
    Fixture           fixture;
    const std::string bufferPath = "/tmp/coverage-gutter-test-reload.py";
    fixture.buffer.InsertAtPoint("a = 1\n");
    fixture.buffer.SetPath(bufferPath);

    const std::string infoPath = "/tmp/coverage-gutter-test-reload.info";
    WriteInfoFile(infoPath, "SF:" + bufferPath + "\nDA:1,0\nend_of_record\n");
    SetCoverageFile(infoPath);
    LoadCoverageReport();

    BufferView      view = fixture.View();
    ned::ui::Screen firstScreen(60, 5);
    PaintInto(view, firstScreen);
    const int x = CoverageColumnX(fixture.buffer.Content().LineCount());
    REQUIRE(firstScreen.PixelAt(x, 0).background_color == Color::BrightRed);

    WriteInfoFile(infoPath, "SF:" + bufferPath + "\nDA:1,5\nend_of_record\n");
    LoadCoverageReport();
    ned::ui::Screen secondScreen(60, 5);
    PaintInto(view, secondScreen);
    REQUIRE(secondScreen.PixelAt(x, 0).background_color == Color::Green);
}
