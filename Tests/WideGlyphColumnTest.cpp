//
// A wide glyph (CJK, most emoji) is two cells; a combining sequence is one
// glyph in one cell; a control or zero-width character is its hex
// placeholder. Every column walk -- BufferView's VisualColumn,
// ByteOffsetForColumnInLine, SkipToColumn and wrap segmentation, and
// Buffer's goal-column arithmetic -- has to agree with what Paint draws.
//

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Text/Rope.h"
#include "Text/RopeStorage.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/BufferView/Internal.h"
#include "UI/Theme.h"

using ned::text::Rope;
using ned::text::RopeStorage;
using ned::ui::detail::ByteOffsetForColumnInLine;
using ned::ui::detail::ComputeWrapSegments;
using ned::ui::detail::SkipToColumn;
using ned::ui::detail::VisualColumn;

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

    ned::ui::BufferView View() {
        return ned::ui::BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher,
                                   statusMessage, mode, theme);
    }
};

// The row's text from `x` on, a continuation cell contributing nothing --
// what a terminal shows.
std::string RowText(ned::ui::Screen& screen, int x, int y, int count) {
    std::string text;
    for (int i = 0; i < count; ++i) {
        text += screen.PixelAt(x + i, y).character;
    }
    return text;
}

} // namespace

TEST_CASE("A wide glyph advances the visual column by two", "[WideGlyph]") {
    const RopeStorage content{Rope("a漢b")}; // bytes: a=0, 漢=1..3, b=4
    CHECK(VisualColumn(content, 0, 1, 1000) == 1);
    CHECK(VisualColumn(content, 0, 4, 1000) == 3);
    CHECK(VisualColumn(content, 0, 5, 1000) == 4);
}

TEST_CASE("A click on either half of a wide glyph lands on it", "[WideGlyph]") {
    const RopeStorage content{Rope("a漢b")};
    CHECK(ByteOffsetForColumnInLine(content, 0, 5, 1, 4, {}) == 1);
    CHECK(ByteOffsetForColumnInLine(content, 0, 5, 2, 4, {}) == 1);
    CHECK(ByteOffsetForColumnInLine(content, 0, 5, 3, 4, {}) == 4);
}

TEST_CASE("Horizontal scroll never starts a row in the middle of a wide glyph", "[WideGlyph]") {
    const RopeStorage content{Rope("漢字x")};
    const auto        exact = SkipToColumn(content, 0, 7, 2);
    CHECK(exact.offset == 3);
    CHECK(exact.columns == 2);
    // Column 1 is the first glyph's right half: the walk overshoots and says so.
    const auto straddle = SkipToColumn(content, 0, 7, 1);
    CHECK(straddle.offset == 3);
    CHECK(straddle.columns == 2);
    CHECK(VisualColumn(content, 0, straddle.offset, 1000) == straddle.columns);
}

TEST_CASE("Wrapping moves a wide glyph that doesn't fit to the next row", "[WideGlyph]") {
    const RopeStorage content{Rope("漢字漢")};
    const auto        segments = ComputeWrapSegments(content, 0, 9, 5, {});
    REQUIRE(segments.size() == 2);
    CHECK(segments[0].endByte == 6);
    CHECK(segments[1].startByte == 6);
}

TEST_CASE("A combining sequence is one column and one step", "[WideGlyph]") {
    const RopeStorage content{Rope("éx")};
    CHECK(VisualColumn(content, 0, 3, 1000) == 1);
    CHECK(ByteOffsetForColumnInLine(content, 0, 4, 1, 4, {}) == 3);
}

TEST_CASE("A control or zero-width character is as wide as its placeholder", "[WideGlyph]") {
    const RopeStorage content{Rope("a‮b")};
    CHECK(VisualColumn(content, 0, 4, 1000) == 1 + 6);
}

TEST_CASE("Up and down keep the visual column across wide glyphs", "[WideGlyph]") {
    ned::text::Buffer buffer{"scratch"};
    buffer.InsertAtPoint("漢字x\nabcdef\n");

    buffer.SetPoint(6); // after 漢字: column 4
    CHECK(buffer.VisualColumnForByteOffset(0, 6, 4) == 4);
    buffer.MoveToNextLine(4);
    CHECK(buffer.Point() == 8 + 4); // "abcd|ef"

    buffer.SetPoint(8 + 3); // column 3: 字's right half on the line above
    buffer.MoveToPreviousLine(4);
    CHECK(buffer.Point() == 3); // lands on 字 itself
}

TEST_CASE("Paint draws a wide glyph with its continuation and places the cursor past it", "[WideGlyph][BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("a漢b\n");
    fixture.buffer.SetPoint(0);
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
    REQUIRE(view.CursorPosition().has_value());
    const int gutter = view.CursorPosition()->x;

    ned::ui::Screen screen(30, 5);
    view.Paint(ned::ui::Canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4}));
    CHECK(screen.PixelAt(gutter + 1, 0).character == "漢");
    CHECK(screen.PixelAt(gutter + 2, 0).character.empty());
    CHECK(screen.PixelAt(gutter + 3, 0).character == "b");

    fixture.buffer.SetPoint(4);
    REQUIRE(view.CursorPosition().has_value());
    CHECK(view.CursorPosition()->x == gutter + 3);
}

TEST_CASE("Paint keeps a combining sequence in one cell and shows invisible characters", "[WideGlyph][BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("éx​y\n");
    fixture.buffer.SetPoint(0);
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});
    REQUIRE(view.CursorPosition().has_value());
    const int gutter = view.CursorPosition()->x;

    ned::ui::Screen screen(30, 5);
    view.Paint(ned::ui::Canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4}));
    CHECK(screen.PixelAt(gutter, 0).character == "é");
    CHECK(RowText(screen, gutter + 1, 0, 8) == "x◁200B▷y");
}

TEST_CASE("A wide glyph with no room at the row's end gives way to the truncation indicator",
          "[WideGlyph][BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string(40, 'a') + "漢漢漢\n");
    fixture.buffer.SetPoint(0);
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4});

    ned::ui::Screen screen(30, 5);
    view.Paint(ned::ui::Canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 4}));
    std::vector<ned::ui::Cell> row;
    for (int x = 0; x < 30; ++x) {
        row.push_back(screen.PixelAt(x, 0));
    }
    std::vector<ned::ui::CellDraw> draws;
    ned::ui::ResolveRowDraws(row, draws);
    for (const ned::ui::CellDraw draw : draws) {
        CHECK(draw != ned::ui::CellDraw::Blank);
    }
}
