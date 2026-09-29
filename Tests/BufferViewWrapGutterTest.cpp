//
// A soft-wrapped line's continuation rows carry on the gutter marks that
// describe the whole line -- diff, unsaved-change status, the selection wash,
// fold guides -- and leave the per-line markers (the number, a diagnostic, a
// breakpoint) on the row the line starts on. A fold guide that stops at every
// wrapped line reads as several separate blocks.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/Provider.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::vcs::DiffHunk;
using ned::ui::BufferView;

namespace {

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

constexpr int kWidth  = 30;
constexpr int kHeight = 12;

void PaintInto(BufferView& view, ned::ui::Screen& screen) {
    const ned::ui::Box box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1};
    view.SetBox_(box);
    ned::ui::Canvas canvas(screen, box);
    view.Paint(canvas);
}

// Long enough to take three rows at this width.
const std::string kLongLine = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda";

} // namespace

TEST_CASE("Diff and status marks repeat on every row of a wrapped line", "[BufferView][Wrap][Vcs]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint("one\n" + kLongLine + "\nthree\n");
    BufferView view = fixture.View();

    SECTION("an added line") {
        view.DispatchDiffForTesting({DiffHunk{1, 0, 2, 1}});
        ned::ui::Screen screen{kWidth, kHeight};
        PaintInto(view, screen);

        // Row 0 is "one"; the long line starts on row 1 and wraps onto 2.
        REQUIRE(screen.PixelAt(0, 1).character == "+");
        CHECK(screen.PixelAt(0, 2).character == "+");
        CHECK(screen.PixelAt(0, 2).foreground_color == fixture.theme.successForeground);
        CHECK(screen.PixelAt(1, 2).background_color == fixture.theme.unsavedChangeIndicator);
    }

    SECTION("a deletion notch marks only the line's top edge") {
        view.DispatchDiffForTesting({DiffHunk{2, 1, 1, 0}});
        ned::ui::Screen screen{kWidth, kHeight};
        PaintInto(view, screen);

        REQUIRE(screen.PixelAt(0, 1).character == "▔");
        CHECK(screen.PixelAt(0, 2).character == " ");
    }
}

TEST_CASE("A selected wrapped line's continuation rows keep the gutter's selection wash", "[BufferView][Wrap]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint("one\n" + kLongLine + "\nthree\n");
    const std::size_t lineStart = 4;
    fixture.buffer.SetMark(lineStart);
    fixture.buffer.SetPoint(lineStart + kLongLine.size() + 1);
    BufferView view = fixture.View();

    ned::ui::Screen screen{kWidth, kHeight};
    PaintInto(view, screen);

    // The digits column's first cell: the number sits right-aligned in it,
    // and the continuation glyph in its last cell.
    int digitsColumn = -1;
    for (int x = 0; x < kWidth; ++x) {
        if (screen.PixelAt(x, 1).character == "2") {
            digitsColumn = x;
            break;
        }
    }
    REQUIRE(digitsColumn >= 0);
    const ned::ui::Color washed = screen.PixelAt(digitsColumn, 1).background_color;
    REQUIRE(washed != fixture.theme.background);
    CHECK(screen.PixelAt(digitsColumn, 2).background_color == washed);
    CHECK(screen.PixelAt(digitsColumn, 2).character != "2");
    // "one" is not selected.
    CHECK(screen.PixelAt(digitsColumn, 0).background_color == fixture.theme.background);
}

TEST_CASE("Fold guides run through a wrapped line's continuation rows", "[BufferView][Wrap][Fold]") {
    Fixture fixture;
    fixture.mode           = ned::editor::CMode();
    fixture.mode.wrapLines = true;
    // The header, a body line and the closer each wrap.
    fixture.buffer.InsertAtPoint("int main(int argument_count, char **argument_values) {\n"
                                 "    return compute_the_answer(argument_count, argument_values);\n"
                                 "} /* the closing brace's own trailing comment wraps too */\n"
                                 "int after;\n");
    BufferView view = fixture.View();

    ned::ui::Screen screen{kWidth, kHeight};
    PaintInto(view, screen);

    int foldColumn = -1;
    for (int x = 0; x < kWidth; ++x) {
        if (screen.PixelAt(x, 0).character == "⊟") {
            foldColumn = x;
            break;
        }
    }
    REQUIRE(foldColumn >= 0);

    std::string column;
    int         closerRow = -1;
    for (int row = 0; row < kHeight; ++row) {
        const std::string glyph = screen.PixelAt(foldColumn, row).character;
        column += glyph == " " ? "." : glyph;
        if (glyph == "└") {
            closerRow = row;
        }
    }
    INFO("fold column: " << column);
    REQUIRE(closerRow > 2);

    // Unbroken from the header's toggle down to the closer's corner...
    for (int row = 1; row < closerRow; ++row) {
        CHECK(screen.PixelAt(foldColumn, row).character == "│");
    }
    // ...and nothing below it, including the closer's own continuation rows.
    for (int row = closerRow + 1; row < kHeight; ++row) {
        CHECK(screen.PixelAt(foldColumn, row).character == " ");
    }
    // The closer really did wrap, or the last check proved nothing.
    bool closerWraps = false;
    for (int x = 0; x < foldColumn; ++x) {
        closerWraps = closerWraps || screen.PixelAt(x, closerRow + 1).character == "\u21b3";
    }
    CHECK(closerWraps);
}
