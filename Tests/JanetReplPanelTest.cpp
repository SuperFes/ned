//
// JanetReplPanel (Source/UI/JanetReplPanel.h) -- headless coverage over the
// shared process-wide Janet Environment (JanetTestSupport.h), driven through
// EvaluateForTesting rather than real key events (TerminalPanel::Feed's own
// "public test seam" convention) and read back via Screen::PixelAt, mirroring
// BufferViewStickyScrollTest.cpp's own RowText helper.
//

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "JanetTestSupport.h"
#include "UI/JanetReplPanel.h"
#include "UI/Widget.h"

namespace {

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::JanetReplPanel;
using ned::ui::Screen;
using ned::ui::Theme;

constexpr int kWidth  = 60;
constexpr int kHeight = 6; // 5 content rows + 1 input row

std::string RowText(Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

// Concatenates every content row (everything but the input row) -- simplest
// way to assert "this text appears somewhere in the transcript" without
// hardcoding exactly which row a given history_ entry lands on.
std::string TranscriptText(Screen& screen) {
    std::string out;
    for (int row = 0; row < kHeight - 1; ++row) {
        out += RowText(screen, row, kWidth);
        out += '\n';
    }
    return out;
}

struct Fixture {
    Theme          theme = ned::ui::DarkTheme();
    JanetReplPanel panel{theme};
    Screen         screen{kWidth, kHeight};

    void Paint() {
        Canvas canvas(screen, Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
        panel.Paint(canvas);
    }
};

} // namespace

TEST_CASE("EvaluateForTesting shows a successful result in the transcript", "[JanetReplPanel]") {
    Fixture fixture;
    fixture.panel.SetEnv(ned_tests::TestEnvironment().Env());

    fixture.panel.EvaluateForTesting("(+ 1 2)");
    fixture.Paint();

    const std::string transcript = TranscriptText(fixture.screen);
    REQUIRE(transcript.find("> (+ 1 2)") != std::string::npos);
    REQUIRE(transcript.find("3") != std::string::npos);
}

TEST_CASE("EvaluateForTesting shows a Janet error in the transcript", "[JanetReplPanel]") {
    Fixture fixture;
    fixture.panel.SetEnv(ned_tests::TestEnvironment().Env());

    fixture.panel.EvaluateForTesting("(error \"boom\")");
    fixture.Paint();

    const std::string transcript = TranscriptText(fixture.screen);
    REQUIRE(transcript.find("boom") != std::string::npos);
}

TEST_CASE("EvaluateForTesting reports no environment when unset", "[JanetReplPanel]") {
    Fixture fixture; // SetEnv never called

    fixture.panel.EvaluateForTesting("(+ 1 2)");
    fixture.Paint();

    const std::string transcript = TranscriptText(fixture.screen);
    REQUIRE(transcript.find("No Janet environment available.") != std::string::npos);
}

// live-smoke-test follow-up: caught interactively -- a nil result (bare
// `nil`, or any void-returning call like ned/set-repl-command) used to
// render as a blank transcript line instead of the text "nil", reading as
// "nothing happened" when the call had actually succeeded.
TEST_CASE("A nil result displays as the text \"nil\", not a blank line", "[JanetReplPanel]") {
    Fixture fixture;
    fixture.panel.SetEnv(ned_tests::TestEnvironment().Env());

    fixture.panel.EvaluateForTesting("nil");
    fixture.Paint();

    const std::string transcript = TranscriptText(fixture.screen);
    REQUIRE(transcript.find("> nil") != std::string::npos);
    REQUIRE(transcript.find("\nnil") != std::string::npos);
}

TEST_CASE("TitleText is just \"Janet REPL\" with no session active", "[JanetReplPanel]") {
    Fixture fixture;
    REQUIRE(fixture.panel.TitleText() == "Janet REPL");
}
