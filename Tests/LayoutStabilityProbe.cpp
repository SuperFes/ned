#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

// A diagnostic, not an assertion -- hidden by Catch2's leading-dot convention,
// run with `ned_tests "[layoutprobe]"`.
//
// "Lines move around in a wonky way while typing" was reported twice against a
// real editing session and is a row-count question, so answer it by counting
// rows rather than by watching a terminal. Each frame records where every
// buffer line landed on screen and how wide the gutter was; anything that
// moves without the text above it changing length is the thing to explain.
TEST_CASE(". LAYOUTPROBE: what moves on screen while typing", "[.][layoutprobe]") {
    std::ifstream      in("Source/UI/BufferView/Paint.cpp");
    std::ostringstream content;
    content << in.rdbuf();
    const std::string source = content.str();
    REQUIRE(source.size() > 10000);

    ned::text::Buffer buffer{"Paint.cpp"};
    buffer.InsertAtPoint(source);

    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode       mode  = ned::editor::CppMode();
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             status;
    ned::ui::ActiveBuffer   active{buffer};

    ned::ui::BufferView view(active, killRing, registers, promptHistory, bufferList, dispatcher, status, mode, theme);
    const ned::ui::Box  box{.x_min = 0, .x_max = 159, .y_min = 0, .y_max = 44};
    view.SetBox_(box);
    ned::ui::Screen screen(160, 45);
    buffer.SetPoint(source.size() / 2);

    // GutterWidth() is private, so measure it the way the eye does: typing
    // one character on one line must advance the cursor exactly one column.
    // Any other delta means the gutter changed width underneath, which shifts
    // every line on screen horizontally.
    std::vector<int> cursorColumns;

    // Where the row carrying the cursor's own line lands, per frame. If this
    // wanders while the lines above it are untouched, something above is
    // adding or removing rows.
    std::vector<int> cursorRows;

    for (int i = 0; i < 40; ++i) {
        view.OnEvent(ned::ui::test::Character('x'));
        ned::ui::Canvas c(screen, box);
        view.Paint(c);
        const std::optional<ned::ui::Point> cursor = view.CursorPosition();
        cursorRows.push_back(cursor ? cursor->y : -1);
        cursorColumns.push_back(cursor ? cursor->x : -1);
    }

    int horizontalJumps = 0;
    for (std::size_t i = 1; i < cursorColumns.size(); ++i) {
        if (cursorColumns[i] - cursorColumns[i - 1] != 1) {
            ++horizontalJumps;
        }
    }
    WARN("  horizontal jumps (cursor advanced by other than 1 column): " << horizontalJumps);

    int moves = 0;
    for (std::size_t i = 1; i < cursorRows.size(); ++i) {
        if (cursorRows[i] != cursorRows[i - 1]) {
            ++moves;
        }
    }
    std::ostringstream rows;
    for (const int r : cursorRows) {
        rows << r << ' ';
    }
    WARN("  cursor row moved " << moves << " times while typing on one line");
    WARN("  rows: " << rows.str());
}

// Follow-up report: "colours, underlines, bolds and italics start wrapping
// weird, but the text stays where it should". Dump the style map beside the
// text so a mismatch between a run of styling and the run of characters it
// belongs to is visible directly.
TEST_CASE(". LAYOUTPROBE: style runs against the text they belong to", "[.][layoutprobe]") {
    const std::string source = "int alpha = 1;  // a trailing comment that is quite long indeed\n"
                               "const char* beta = \"a string literal\";\n"
                               "int gamma = 3;\n";
    ned::text::Buffer buffer{"probe.cpp"};
    buffer.InsertAtPoint(source);

    // Offsets found in the text rather than written by hand: a
    // hand-counted offset that lands two columns early looks exactly like
    // the misalignment this probe exists to detect, which is a good way to
    // spend an hour chasing your own test data.
    const auto span = [&](std::string_view word) {
        const std::size_t at = source.find(word);
        return std::pair<std::size_t, std::size_t>{at, at + word.size()};
    };
    const auto [alphaStart, alphaEnd] = span("alpha");
    const auto [betaStart, betaEnd]   = span("beta");
    const auto [gammaStart, gammaEnd] = span("gamma");
    buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{.startByte = alphaStart,
                                      .endByte   = alphaEnd,
                                      .severity  = ned::text::Buffer::Diagnostic::Severity::Warning,
                                      .message   = "unused variable alpha"},
        ned::text::Buffer::Diagnostic{.startByte = betaStart,
                                      .endByte   = betaEnd,
                                      .severity  = ned::text::Buffer::Diagnostic::Severity::Error,
                                      .message   = "expected ';' after declaration"},
        ned::text::Buffer::Diagnostic{.startByte = gammaStart,
                                      .endByte   = gammaEnd,
                                      .severity  = ned::text::Buffer::Diagnostic::Severity::Warning,
                                      .message   = "unused variable gamma"},
    });
    buffer.SetPoint(0);

    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode       mode  = ned::editor::CppMode();
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             status;
    ned::ui::ActiveBuffer   active{buffer};

    ned::ui::BufferView view(active, killRing, registers, promptHistory, bufferList, dispatcher, status, mode, theme);
    const ned::ui::Box  box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 7};
    view.SetBox_(box);
    ned::ui::Screen screen(60, 8);
    ned::ui::Canvas canvas(screen, box);
    view.Paint(canvas);

    for (int row = 0; row < 8; ++row) {
        std::string text;
        std::string style;
        for (int x = 0; x < 60; ++x) {
            const ned::ui::Cell& cell = screen.PixelAt(x, row);
            text += cell.character.empty() ? " " : cell.character;
            char s = '.';
            if (cell.italic) {
                s = 'i';
            }
            if (cell.bold) {
                s = 'b';
            }
            if (cell.underlined) {
                s = 'u';
            }
            if (cell.bold && cell.italic) {
                s = 'B';
            }
            style += s;
        }
        WARN("  text  |" << text << "|");
        WARN("  style |" << style << "|");
    }
}
