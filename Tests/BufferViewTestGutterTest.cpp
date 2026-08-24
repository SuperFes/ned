#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/TestRun/TestRunConfig.h"
#include "Editor/TestRun/TestRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using ned::editor::testrun::MatchesTestName;
using ned::editor::testrun::SetTestCommand;
using ned::editor::testrun::TestRunner;
using ned::ui::BufferView;
using ned::ui::Color;

namespace {

// [status][diagnostic][gap][digits][gap][test][symbol] -- the same layout
// arithmetic BufferViewSymbolGutterTest's own helper tracks, plus the test
// column. Content stays single-line-per-definition so PythonMode's fold
// query never reserves its own column here (the symbol test's precedent).
int TestColumnX(std::size_t totalLines) {
    constexpr int kStatusWidth     = 1;
    constexpr int kDiagnosticWidth = 1;
    constexpr int kLineNumberGap   = 1;
    return kStatusWidth + kDiagnosticWidth + kLineNumberGap + static_cast<int>(std::to_string(totalLines).size()) +
           kLineNumberGap;
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
        SetTestCommand({}, "");
    }
};

void PaintInto(BufferView& view, ned::ui::Screen& screen) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 4});
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 4});
    view.Paint(canvas);
}

} // namespace

TEST_CASE("MatchesTestName accepts the framework-qualified shapes", "[TestRun]") {
    CHECK(MatchesTestName("test_ok", "test_ok"));
    CHECK(MatchesTestName("test_param", "test_param[2]"));            // parameterized instance
    CHECK(MatchesTestName("TestSub", "TestSub/child_fail"));          // go subtest onto its parent
    CHECK(MatchesTestName("testBar", "Tests\\FooTest::testBar"));     // PHPUnit Class::method
    CHECK(MatchesTestName("test_method", "TestThings::test_method")); // pytest class node id
    CHECK(MatchesTestName("CaseName", "SuiteName.CaseName"));         // gtest Suite.Name
    CHECK(MatchesTestName("test_x", "module.TestClass::test_x[3]"));  // qualified + parameterized

    CHECK_FALSE(MatchesTestName("test_ok", "test_okay"));
    CHECK_FALSE(MatchesTestName("TestSub", "TestSubmarine"));
    CHECK_FALSE(MatchesTestName("", "anything"));
    CHECK_FALSE(MatchesTestName("anything", ""));
}

TEST_CASE("Test gutter reserves nothing without a runner or without an outcome", "[BufferView][TestRun]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("def test_ok(): pass\n");

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 4});
    REQUIRE(view.CursorPosition().has_value());
    // test column absent, symbol column present (a def is a symbol marker).
    const int withoutTestColumn = view.CursorPosition()->x;

    // Now with a runner wired but no outcome yet: still nothing reserved.
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(fixture.bufferList, eventLoop);
    view.SetTestRunner(&runner);
    REQUIRE(view.CursorPosition()->x == withoutTestColumn);
}

TEST_CASE("Test gutter marks discovered tests with status glyphs after a parsed run", "[BufferView][TestRun]") {
    ConfigResetGuard   guard;
    Fixture            fixture;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(fixture.bufferList, eventLoop);
    fixture.buffer.InsertAtPoint("def test_ok(): pass\n"
                                 "def test_fails(): assert False\n"
                                 "def test_skipped(): pass\n"
                                 "def helper(): pass\n");

    SetTestCommand({"true"}, "pytest");
    runner.DispatchProcessOutput("collected 3 items\n"
                                 "\n"
                                 "sample.py::test_ok PASSED                                    [ 33%]\n"
                                 "sample.py::test_fails FAILED                                 [ 66%]\n"
                                 "sample.py::test_skipped SKIPPED (later)                      [100%]\n"
                                 "\n"
                                 "==================== 1 failed, 1 passed, 1 skipped in 0.01s ====================\n");
    runner.DispatchProcessExit(0);

    BufferView view = fixture.View();
    view.SetTestRunner(&runner);
    ned::ui::Screen screen(60, 5);
    PaintInto(view, screen);

    const int testX = TestColumnX(fixture.buffer.Content().LineCount());
    CHECK(screen.PixelAt(testX, 0).character == "✓");
    CHECK(screen.PixelAt(testX, 0).foreground_color == Color::BrightGreen);
    CHECK(screen.PixelAt(testX, 1).character == "✗");
    CHECK(screen.PixelAt(testX, 1).foreground_color == Color::BrightRed);
    CHECK(screen.PixelAt(testX, 2).character == "−");
    CHECK(screen.PixelAt(testX, 2).foreground_color == Color::BrightYellow);
    CHECK(screen.PixelAt(testX, 3).character == " "); // helper() is not a test
}

TEST_CASE("Test gutter cache refreshes when a new outcome generation lands", "[BufferView][TestRun]") {
    ConfigResetGuard   guard;
    Fixture            fixture;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(fixture.bufferList, eventLoop);
    fixture.buffer.InsertAtPoint("def test_flaky(): pass\n");
    SetTestCommand({"true"}, "pytest");

    // RunAll spawns a real short-lived process (whose posted callbacks never
    // run here -- no event loop) and, crucially, resets the accumulator
    // between runs; the manual dispatches stand in for the real callbacks.
    runner.RunAll();
    runner.DispatchProcessOutput("sample.py::test_flaky FAILED\n"
                                 "==================== 1 failed in 0.01s ====================\n");
    runner.DispatchProcessExit(0);

    BufferView view = fixture.View();
    view.SetTestRunner(&runner);
    ned::ui::Screen firstScreen(60, 5);
    PaintInto(view, firstScreen);
    const int testX = TestColumnX(fixture.buffer.Content().LineCount());
    REQUIRE(firstScreen.PixelAt(testX, 0).character == "✗");

    runner.RunAll();
    runner.DispatchProcessOutput("sample.py::test_flaky PASSED\n"
                                 "==================== 1 passed in 0.01s ====================\n");
    runner.DispatchProcessExit(0);
    ned::ui::Screen secondScreen(60, 5);
    PaintInto(view, secondScreen);
    REQUIRE(secondScreen.PixelAt(testX, 0).character == "✓");
}

TEST_CASE("failuresOnly outcomes infer a pass mark only for a full, parsed run", "[BufferView][TestRun]") {
    ConfigResetGuard   guard;
    Fixture            fixture;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(fixture.bufferList, eventLoop);
    // Catch2-style: only the failing test is ever named in the output.
    fixture.mode = ned::editor::CppMode();
    fixture.buffer.InsertAtPoint("TEST_CASE(\"green one\") { CHECK(true); }\n"
                                 "TEST_CASE(\"red one\") { CHECK(false); }\n");
    SetTestCommand({"true"}, "catch2");

    const std::string dashes(79, '-');
    const std::string dots(79, '.');
    runner.DispatchProcessOutput(dashes + "\n" + "red one\n" + dashes + "\n" + "sample.cpp:2\n" + dots + "\n\n" +
                                 "sample.cpp:2: FAILED:\n  CHECK( false )\n\n" +
                                 "test cases: 2 | 1 passed | 1 failed\n");
    runner.DispatchProcessExit(1);

    BufferView view = fixture.View();
    view.SetTestRunner(&runner);
    ned::ui::Screen screen(60, 5);
    PaintInto(view, screen);

    const int testX = TestColumnX(fixture.buffer.Content().LineCount());
    // "green one" never appears in the output -- inferred ✓ from the clean
    // full run under a failures-only format.
    CHECK(screen.PixelAt(testX, 0).character == "✓");
    CHECK(screen.PixelAt(testX, 1).character == "✗");
}

TEST_CASE("A basename mismatch filters out a cross-file name collision", "[BufferView][TestRun]") {
    ConfigResetGuard   guard;
    Fixture            fixture;
    ned::ui::EventLoop eventLoop;
    TestRunner         runner(fixture.bufferList, eventLoop);
    fixture.buffer.InsertAtPoint("def test_shared_name(): pass\n");
    // A file-backed buffer named other.py must reject a result reported
    // against sample.py even though the name matches.
    fixture.buffer.SetPath("/tmp/other.py");
    SetTestCommand({"true"}, "pytest");

    runner.DispatchProcessOutput("sample.py::test_shared_name FAILED\n"
                                 "==================== 1 failed in 0.01s ====================\n");
    runner.DispatchProcessExit(0);

    BufferView view = fixture.View();
    view.SetTestRunner(&runner);
    ned::ui::Screen screen(60, 5);
    PaintInto(view, screen);

    const int testX = TestColumnX(fixture.buffer.Content().LineCount());
    CHECK(screen.PixelAt(testX, 0).character != "✗"); // not marked failed from another file's result
}
