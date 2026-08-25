#include <catch2/catch_test_macros.hpp>

#include "Editor/Vim/VimMotion.h"
#include "Text/Buffer.h"

using ned::editor::vim::CharLeft;
using ned::editor::vim::CharRight;
using ned::editor::vim::FindChar;
using ned::editor::vim::FirstNonBlankMotion;
using ned::editor::vim::GotoFirstLine;
using ned::editor::vim::GotoLastLine;
using ned::editor::vim::LineDown;
using ned::editor::vim::LineEndMotion;
using ned::editor::vim::LineStartMotion;
using ned::editor::vim::LineUp;
using ned::editor::vim::MatchPair;
using ned::editor::vim::ParagraphBackward;
using ned::editor::vim::ParagraphForward;
using ned::editor::vim::WordBackward;
using ned::editor::vim::WordEndBackward;
using ned::editor::vim::WordEndForward;
using ned::editor::vim::WordForward;
using ned::text::Buffer;

namespace {
Buffer MakeBuffer(const std::string& text) {
    Buffer buffer("test");
    buffer.InsertAtPoint(text);
    buffer.SetPoint(0);
    return buffer;
}
} // namespace

TEST_CASE("h/l move within a line and stop at its edges", "[VimMotion]") {
    Buffer buffer = MakeBuffer("abcde");

    REQUIRE(CharRight(buffer, 0, 2).target == 2);
    REQUIRE(CharRight(buffer, 4, 5).target == 4); // clamped at last char
    REQUIRE(CharLeft(buffer, 4, 2).target == 2);
    REQUIRE(CharLeft(buffer, 0, 1).target == 0);
}

TEST_CASE("h/l do not cross line boundaries", "[VimMotion]") {
    Buffer buffer = MakeBuffer("ab\ncd");

    REQUIRE(CharRight(buffer, 0, 10).target == 1); // stops at 'b', not the newline
    REQUIRE(CharLeft(buffer, 3, 10).target == 3);  // 'c' is already line start
}

TEST_CASE("0/^/$ within a line", "[VimMotion]") {
    Buffer buffer = MakeBuffer("  abc  \n");

    REQUIRE(LineStartMotion(buffer, 5).target == 0);
    REQUIRE(FirstNonBlankMotion(buffer, 5).target == 2);
    const auto end = LineEndMotion(buffer, 0, 1);
    REQUIRE(end.target == 6); // last char before trailing spaces' final char -- "  abc  " index 6 is the last space
    REQUIRE(end.inclusive);
}

TEST_CASE("j/k move by sticky goal column", "[VimMotion]") {
    Buffer buffer = MakeBuffer("abcdef\nab\nabcdef\n");

    // Start on column 4 of line 0 ('e'), move down: line 1 is short, goal column
    // sticks at 4 for when a longer line comes back.
    const auto down1 = LineDown(buffer, 4, 1, 4, 1);
    REQUIRE(down1.target == buffer.ByteOffsetForLineAndColumn(1, 4, 1)); // clamped onto short line
    const auto down2 = LineDown(buffer, down1.target, 1, 4, 1);
    REQUIRE(down2.target == buffer.ByteOffsetForLineAndColumn(2, 4, 1)); // back to column 4
    REQUIRE(down2.linewise);

    const auto up = LineUp(buffer, down2.target, 2, 4, 1);
    REQUIRE(up.target == buffer.ByteOffsetForLineAndColumn(0, 4, 1));
}

TEST_CASE("gg/G with and without a count", "[VimMotion]") {
    Buffer buffer = MakeBuffer("one\ntwo\nthree\n");

    REQUIRE(GotoFirstLine(buffer, 0).target == 0);
    REQUIRE(GotoLastLine(buffer, 0).target == buffer.Content().LineToByteOffset(2));
    REQUIRE(GotoFirstLine(buffer, 2).target == buffer.Content().LineToByteOffset(1));
}

TEST_CASE("w/b/e word motion over word/punctuation/blank runs", "[VimMotion]") {
    Buffer buffer = MakeBuffer("foo.bar  baz");

    const auto w1 = WordForward(buffer, 0, 1, false);
    REQUIRE(w1.target == 3); // '.'
    const auto w2 = WordForward(buffer, w1.target, 1, false);
    REQUIRE(w2.target == 4); // 'b' of bar
    const auto w3 = WordForward(buffer, w2.target, 1, false);
    REQUIRE(w3.target == 9); // 'b' of baz, blanks skipped

    REQUIRE(WordBackward(buffer, w3.target, 1, false).target == w2.target);

    const auto e1 = WordEndForward(buffer, 0, 1, false);
    REQUIRE(e1.target == 2); // end of "foo"
    REQUIRE(e1.inclusive);
}

TEST_CASE("ge moves backward to the end of the previous word/punctuation run", "[VimMotion]") {
    Buffer buffer = MakeBuffer("foo.bar  baz");

    const auto g1 = WordEndBackward(buffer, 11, 1, false); // from 'z' of baz
    REQUIRE(g1.target == 6);                               // end of "bar"
    REQUIRE(g1.inclusive);

    const auto g2 = WordEndBackward(buffer, g1.target, 1, false); // from end of "bar"
    REQUIRE(g2.target == 3);                                      // '.'

    const auto g3 = WordEndBackward(buffer, g2.target, 1, false); // from '.'
    REQUIRE(g3.target == 2);                                      // end of "foo"

    REQUIRE(WordEndBackward(buffer, 0, 1, false).target == 0); // no previous word: clamps
}

TEST_CASE("gE treats punctuation as part of the WORD", "[VimMotion]") {
    Buffer buffer = MakeBuffer("foo.bar  baz");

    const auto g = WordEndBackward(buffer, 11, 1, true); // from 'z' of baz
    REQUIRE(g.target == 6);                              // end of the whole "foo.bar" WORD
}

TEST_CASE("W/B/E treat punctuation as part of the WORD", "[VimMotion]") {
    Buffer buffer = MakeBuffer("foo.bar  baz");

    const auto w = WordForward(buffer, 0, 1, true);
    REQUIRE(w.target == 9); // whole "foo.bar" is one WORD, blanks skipped
}

TEST_CASE("w stops on an empty line as its own word", "[VimMotion]") {
    Buffer buffer = MakeBuffer("abc\n\ndef");

    const auto w = WordForward(buffer, 0, 1, false);
    REQUIRE(w.target == buffer.Content().LineToByteOffset(1)); // the empty line itself
}

TEST_CASE("f/t find a character on the current line, failing past its end", "[VimMotion]") {
    Buffer buffer = MakeBuffer("abcdef");

    const auto f = FindChar(buffer, 0, 1, U'd', true, false);
    REQUIRE(f.found);
    REQUIRE(f.target == 3);

    const auto t = FindChar(buffer, 0, 1, U'd', true, true);
    REQUIRE(t.found);
    REQUIRE(t.target == 2);

    const auto miss = FindChar(buffer, 0, 1, U'z', true, false);
    REQUIRE_FALSE(miss.found);
}

TEST_CASE("F/T find a character backward on the current line", "[VimMotion]") {
    Buffer buffer = MakeBuffer("abcdef");

    const auto F = FindChar(buffer, 5, 1, U'b', false, false);
    REQUIRE(F.found);
    REQUIRE(F.target == 1);

    const auto T = FindChar(buffer, 5, 1, U'b', false, true);
    REQUIRE(T.found);
    REQUIRE(T.target == 2);
}

TEST_CASE("{ and } move between blank-line paragraph boundaries", "[VimMotion]") {
    Buffer buffer = MakeBuffer("a\nb\n\nc\nd\n\ne\n");

    const auto p1 = ParagraphForward(buffer, 0, 1);
    REQUIRE(p1.target == buffer.Content().LineToByteOffset(2));
    const auto p2 = ParagraphForward(buffer, p1.target, 1);
    REQUIRE(p2.target == buffer.Content().LineToByteOffset(5));

    const auto back = ParagraphBackward(buffer, p2.target, 1);
    REQUIRE(back.target == p1.target);
}

TEST_CASE("% jumps to the matching bracket", "[VimMotion]") {
    Buffer buffer = MakeBuffer("foo(bar(baz)qux)end");

    const auto m1 = MatchPair(buffer, 0);
    REQUIRE(m1.found);
    REQUIRE(m1.target == 15); // the outer ')'

    const auto m2 = MatchPair(buffer, 15);
    REQUIRE(m2.found);
    REQUIRE(m2.target == 3); // back to the outer '('
}
