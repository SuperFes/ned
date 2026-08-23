#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Rectangle.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::ComputeRectangleBounds;
using ned::editor::DeleteRectangle;
using ned::editor::GlobalRectangleClipboard;
using ned::editor::KillRectangle;
using ned::editor::RectangleClipboard;
using ned::editor::SetRectangleClipboard;
using ned::editor::StringRectangle;
using ned::editor::YankRectangle;
using ned::text::Buffer;
using ned::text::Rope;

TEST_CASE("ComputeRectangleBounds with point and mark on the same line", "[Rectangle]") {
    Buffer buffer("test", Rope("abcdef\nghijkl\nmnopqr"));
    buffer.SetPoint(2);
    buffer.SetMark(5);

    const auto bounds = ComputeRectangleBounds(buffer, 1);
    REQUIRE(bounds.startLine == 0);
    REQUIRE(bounds.endLine == 0);
    REQUIRE(bounds.startColumn == 2);
    REQUIRE(bounds.endColumn == 5);
}

TEST_CASE("ComputeRectangleBounds with point and mark on different lines, byte order matching column order",
          "[Rectangle]") {
    Buffer buffer("test", Rope("abcdef\nghijkl\nmnopqr"));
    buffer.SetPoint(1); // line 0, column 1
    buffer.SetMark(18); // line 2, column 4 (line 2 starts at byte 14)

    const auto bounds = ComputeRectangleBounds(buffer, 1);
    REQUIRE(bounds.startLine == 0);
    REQUIRE(bounds.endLine == 2);
    REQUIRE(bounds.startColumn == 1);
    REQUIRE(bounds.endColumn == 4);
}

TEST_CASE("ComputeRectangleBounds computes columns from each endpoint independently, not byte-linear order",
          "[Rectangle]") {
    // mark: line 0, column 5 (a *high* column on an *earlier* line, byte 5).
    // point: line 2, column 1 (a *low* column on a *later* line, byte 15).
    // Byte-linear order (mark=5 < point=15) does NOT match column order
    // (mark's column 5 > point's column 1) -- if bounds were derived from
    // Region()'s linear min/max instead of each endpoint's own line+column,
    // this would produce the wrong rectangle shape.
    Buffer buffer("test", Rope("abcdef\nghijkl\nmnopqr"));
    buffer.SetMark(5);
    buffer.SetPoint(15);

    const auto bounds = ComputeRectangleBounds(buffer, 1);
    REQUIRE(bounds.startLine == 0);
    REQUIRE(bounds.endLine == 2);
    REQUIRE(bounds.startColumn == 1);
    REQUIRE(bounds.endColumn == 5);
}

TEST_CASE("KillRectangle removes a column range across lines and saves it to the clipboard", "[Rectangle]") {
    Buffer buffer("test", Rope("abcdef\nghijkl"));
    buffer.SetMark(1);   // line 0, column 1
    buffer.SetPoint(11); // line 1, column 4 (line 1 starts at byte 7)

    KillRectangle(buffer, 1);

    REQUIRE(buffer.Text() == "aef\ngkl");
    REQUIRE_FALSE(buffer.HasMark());
    REQUIRE(GlobalRectangleClipboard().Lines() == std::vector<std::string>{"bcd", "hij"});
}

TEST_CASE("DeleteRectangle removes a column range without touching the clipboard", "[Rectangle]") {
    SetRectangleClipboard({"untouched"});

    Buffer buffer("test", Rope("abcdef\nghijkl"));
    buffer.SetMark(1);
    buffer.SetPoint(11);

    DeleteRectangle(buffer, 1);

    REQUIRE(buffer.Text() == "aef\ngkl");
    REQUIRE(GlobalRectangleClipboard().Lines() == std::vector<std::string>{"untouched"});
}

TEST_CASE("KillRectangle on a rectangle where one line is shorter than the column range is a safe no-op for that line",
          "[Rectangle]") {
    // The short line ("ab", 2 columns) sits *between* the two endpoint
    // lines, both of which are long enough that mark/point's own columns
    // (3 and 5) are real, unclamped positions on those lines -- placing the
    // short line at an endpoint instead would clamp mark/point's own column
    // down to the short line's length, silently changing the rectangle's
    // intended bounds rather than testing a mid-range short line at all.
    Buffer buffer("test", Rope("ghijkl\nab\nmnopqr"));
    buffer.SetMark(buffer.ByteOffsetForLineAndColumn(0, 3, 1));
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(2, 5, 1));

    KillRectangle(buffer, 1);

    REQUIRE(buffer.Text() == "ghil\nab\nmnor"); // line 1 untouched (already shorter than column 3)
    REQUIRE(GlobalRectangleClipboard().Lines() == std::vector<std::string>{"jk", "", "pq"});
}

TEST_CASE("KillRectangle then YankRectangle round-trips, padding a shorter destination line", "[Rectangle]") {
    Buffer buffer("test", Rope("XY\nAB\nCD"));
    buffer.SetMark(0);
    buffer.SetPoint(5); // line 1, column 2 (line 1 starts at byte 3)

    KillRectangle(buffer, 1);
    REQUIRE(buffer.Text() == "\n\nCD");

    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(2, 2, 1)); // end of "CD", column 2
    YankRectangle(buffer, 1);

    // "XY" lands directly (line 2 is already exactly as wide as the target
    // column); "AB" lands on a fresh line 3 that has to be both created
    // (buffer only had 3 lines) and padded with 2 spaces to reach column 2.
    REQUIRE(buffer.Text() == "\n\nCDXY\n  AB");
}

TEST_CASE("YankRectangle pads an existing short line, not just a freshly-created one", "[Rectangle]") {
    Buffer buffer("test", Rope("ABCDE\nXY"));
    SetRectangleClipboard({"11", "22"});

    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(0, 3, 1)); // line 0, column 3

    YankRectangle(buffer, 1);

    REQUIRE(buffer.Text() == "ABC11DE\nXY 22"); // line 1 ("XY", width 2) padded by 1 space to reach column 3
}

TEST_CASE("YankRectangle extends the buffer with a new line when the clipboard has more lines than remain",
          "[Rectangle]") {
    Buffer buffer("test", Rope("AB"));
    SetRectangleClipboard({"X", "Y"});

    buffer.SetPoint(2); // end of "AB", column 2

    YankRectangle(buffer, 1);

    REQUIRE(buffer.Text() == "ABX\n  Y");
}

TEST_CASE("StringRectangle replaces a column range with a shorter and a longer string", "[Rectangle]") {
    Buffer buffer("test", Rope("abcdef\nghijkl"));
    buffer.SetMark(buffer.ByteOffsetForLineAndColumn(0, 1, 1));
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(1, 4, 1));

    StringRectangle(buffer, "XY", 1);

    REQUIRE(buffer.Text() == "aXYef\ngXYkl");
    REQUIRE_FALSE(buffer.HasMark());
}

TEST_CASE("StringRectangle with a replacement longer than the original column range", "[Rectangle]") {
    Buffer buffer("test", Rope("abcdef\nghijkl"));
    buffer.SetMark(buffer.ByteOffsetForLineAndColumn(0, 1, 1));
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(1, 4, 1));

    StringRectangle(buffer, "12345", 1);

    REQUIRE(buffer.Text() == "a12345ef\ng12345kl");
}

TEST_CASE("RectangleClipboard Set/Lines/Empty", "[Rectangle]") {
    RectangleClipboard clipboard;
    REQUIRE(clipboard.Empty());

    clipboard.Set({"a", "b", "c"});
    REQUIRE_FALSE(clipboard.Empty());
    REQUIRE(clipboard.Lines() == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("SetRectangleClipboard/GlobalRectangleClipboard round-trip", "[Rectangle]") {
    SetRectangleClipboard({"x", "y"});
    REQUIRE(GlobalRectangleClipboard().Lines() == std::vector<std::string>{"x", "y"});
}

// multi-cursor-rectangle follow-up.

TEST_CASE("Set is equivalent to SetBlocks with a single block", "[Rectangle]") {
    RectangleClipboard clipboard;

    clipboard.Set({"a", "b"});
    REQUIRE(clipboard.Blocks() == std::vector<std::vector<std::string>>{{"a", "b"}});
    REQUIRE(clipboard.Lines() == std::vector<std::string>{"a", "b"});
}

TEST_CASE("SetBlocks stores multiple blocks; Lines() reads the first", "[Rectangle]") {
    RectangleClipboard clipboard;

    clipboard.SetBlocks({{"1", "2"}, {"x", "y", "z"}});
    REQUIRE(clipboard.Blocks().size() == 2);
    REQUIRE(clipboard.Lines() == std::vector<std::string>{"1", "2"});
    REQUIRE_FALSE(clipboard.Empty());
}

TEST_CASE("RectangleClipboard is Empty only when every block is empty", "[Rectangle]") {
    RectangleClipboard clipboard;
    REQUIRE(clipboard.Empty());

    clipboard.SetBlocks({{}, {}});
    REQUIRE(clipboard.Empty()); // no cursor actually killed anything

    clipboard.SetBlocks({{}, {"row"}});
    REQUIRE_FALSE(clipboard.Empty()); // one cursor did
}

TEST_CASE("YankRectangleLines matches YankRectangle given the same lines", "[Rectangle]") {
    Buffer buffer("test", Rope("ABCDE\nXY"));
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(0, 3, 1));

    ned::editor::YankRectangleLines(buffer, {"11", "22"}, 1);

    REQUIRE(buffer.Text() == "ABC11DE\nXY 22");
}
