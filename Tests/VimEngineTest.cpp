#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/TabWidth.h"
#include "Editor/Vim/VimEngine.h"
#include "Text/Buffer.h"

using ned::editor::KeyChord;
using ned::editor::SpecialKey;
using ned::editor::vim::Mode;
using ned::editor::vim::PendingIntent;
using ned::editor::vim::VimEngine;
using ned::text::Buffer;

namespace {

Buffer MakeBuffer(const std::string& text) {
    Buffer buffer("test");
    buffer.InsertAtPoint(text);
    buffer.SetPoint(0);
    return buffer;
}

KeyChord Ch(char32_t c) {
    KeyChord k;
    k.Codepoint = c;
    return k;
}

KeyChord Special(SpecialKey s) {
    KeyChord k;
    k.Special = s;
    return k;
}

KeyChord Ctrl(char32_t c) {
    KeyChord k;
    k.Control   = true;
    k.Codepoint = c;
    return k;
}

KeyChord CtrlV() {
    return Ctrl(U'v');
}

void Feed(VimEngine& engine, Buffer& buffer, const std::string& keys) {
    for (char c : keys) {
        if (c == '\x1b') {
            engine.HandleKey(buffer, Special(SpecialKey::Escape));
        }
        else if (c == '\n' && engine.CurrentMode() != Mode::Insert) {
            engine.HandleKey(buffer, Special(SpecialKey::Enter));
        }
        else if (engine.CurrentMode() == Mode::Insert) {
            // Live typing bypasses VimEngine::HandleKey in real BufferView usage;
            // simulate that bypass directly here.
            if (c == '\n') {
                buffer.InsertAtPoint("\n");
            }
            else {
                buffer.InsertAtPoint(std::string(1, c));
            }
            engine.RecordInsertKey(Ch(static_cast<unsigned char>(c)));
        }
        else {
            engine.HandleKey(buffer, Ch(static_cast<unsigned char>(c)));
        }
    }
}

} // namespace

TEST_CASE("h/j/k/l move point through the engine", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abc\ndef\n");
    VimEngine engine;

    Feed(engine, buffer, "ll");
    REQUIRE(buffer.Point() == 2);
    Feed(engine, buffer, "j");
    REQUIRE(buffer.Point() == buffer.ByteOffsetForLineAndColumn(1, 2, 1));
}

TEST_CASE("dw deletes a word and stores it in the unnamed register", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar baz");
    VimEngine engine;

    Feed(engine, buffer, "dw");
    REQUIRE(buffer.Text() == "bar baz");
    REQUIRE(buffer.Point() == 0);
}

TEST_CASE("dd deletes the whole current line, linewise", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\nthree\n");
    VimEngine engine;

    Feed(engine, buffer, "jdd");
    REQUIRE(buffer.Text() == "one\nthree\n");
}

TEST_CASE("3dd deletes three lines", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\nthree\nfour\n");
    VimEngine engine;

    Feed(engine, buffer, "3dd");
    REQUIRE(buffer.Text() == "four\n");
}

TEST_CASE("ciw changes the word under point and enters Insert mode", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar baz");
    VimEngine engine;

    buffer.SetPoint(5); // inside "bar"
    Feed(engine, buffer, "ciw");
    REQUIRE(engine.CurrentMode() == Mode::Insert);
    Feed(engine, buffer, "XYZ");
    Feed(engine, buffer, "\x1b");
    REQUIRE(buffer.Text() == "foo XYZ baz");
    REQUIRE(engine.CurrentMode() == Mode::Normal);
}

TEST_CASE("di( deletes inside parens", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo(bar(baz)qux)end");
    VimEngine engine;

    buffer.SetPoint(9); // inside "baz"
    Feed(engine, buffer, "di(");
    REQUIRE(buffer.Text() == "foo(bar()qux)end");
}

TEST_CASE("yy then p yanks and pastes a whole line below", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\n");
    VimEngine engine;

    Feed(engine, buffer, "yyp");
    REQUIRE(buffer.Text() == "one\none\ntwo\n");
}

TEST_CASE("yw then P pastes charwise text before point", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar");
    VimEngine engine;

    Feed(engine, buffer, "yw");
    Feed(engine, buffer, "$"); // go to end of line
    Feed(engine, buffer, "P");
    REQUIRE(buffer.Text() == "foo bafoo r");
}

TEST_CASE("x deletes a character forward and u undoes it", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abc");
    VimEngine engine;

    Feed(engine, buffer, "x");
    REQUIRE(buffer.Text() == "bc");
    Feed(engine, buffer, "u");
    REQUIRE(buffer.Text() == "abc");
}

TEST_CASE("A appends at end of line and o opens a new line below", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abc");
    VimEngine engine;

    Feed(engine, buffer, "A123\x1b");
    REQUIRE(buffer.Text() == "abc123");

    Feed(engine, buffer, "onew\x1b");
    REQUIRE(buffer.Text() == "abc123\nnew");
}

TEST_CASE("O opens a new line above", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abc");
    VimEngine engine;

    Feed(engine, buffer, "Onew\x1b");
    REQUIRE(buffer.Text() == "new\nabc");
}

TEST_CASE("Dot repeats the last change", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar baz qux");
    VimEngine engine;

    Feed(engine, buffer, "dw");
    REQUIRE(buffer.Text() == "bar baz qux");
    Feed(engine, buffer, ".");
    REQUIRE(buffer.Text() == "baz qux");
}

TEST_CASE("Dot repeats an insert-causing change verbatim", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo\nbar\n");
    VimEngine engine;

    Feed(engine, buffer, "AX\x1b");
    REQUIRE(buffer.Text() == "fooX\nbar\n");
    Feed(engine, buffer, "j.");
    REQUIRE(buffer.Text() == "fooX\nbarX\n");
}

TEST_CASE("Visual mode d deletes the selected inclusive range", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abcdef");
    VimEngine engine;

    Feed(engine, buffer, "vlld"); // select 'a','b','c' then delete
    REQUIRE(buffer.Text() == "def");
    REQUIRE(engine.CurrentMode() == Mode::Normal);
}

TEST_CASE("Visual line mode d deletes whole lines", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\nthree\n");
    VimEngine engine;

    Feed(engine, buffer, "Vjd");
    REQUIRE(buffer.Text() == "three\n");
}

TEST_CASE("Named register a stores and pastes independently of unnamed", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar");
    VimEngine engine;

    Feed(engine, buffer, "\"ayw"); // yank "foo " into register a
    Feed(engine, buffer, "dw");    // delete "foo " via unnamed (now unnamed holds "foo ")
    REQUIRE(buffer.Text() == "bar");
    Feed(engine, buffer, "\"ap"); // paste register a's content after point
    REQUIRE(buffer.Text() == "bfoo ar");
}

TEST_CASE("Search with / finds the next match and n repeats", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar foo baz foo");
    VimEngine engine;

    Feed(engine, buffer, "/foo\n");
    // First "/foo" search from point 0 should wrap and land on the second occurrence
    // (search starts just after point).
    REQUIRE(buffer.Point() == 8);
    Feed(engine, buffer, "n");
    REQUIRE(buffer.Point() == 16);
    Feed(engine, buffer, "n"); // wraps back to the first
    REQUIRE(buffer.Point() == 0);
}

TEST_CASE(":d deletes the addressed range", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\nthree\nfour\n");
    VimEngine engine;

    Feed(engine, buffer, ":2,3d\n");
    REQUIRE(buffer.Text() == "one\nfour\n");
}

TEST_CASE(":s substitutes the first match per line without /g", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo foo\nfoo foo\n");
    VimEngine engine;

    Feed(engine, buffer, ":%s/foo/bar/\n");
    REQUIRE(buffer.Text() == "bar foo\nbar foo\n");
}

TEST_CASE(":%s with /g substitutes every match", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo foo\n");
    VimEngine engine;

    Feed(engine, buffer, ":%s/foo/bar/g\n");
    REQUIRE(buffer.Text() == "bar bar\n");
}

TEST_CASE("A bare :42 jumps to that line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("a\nb\nc\nd\ne\n");
    VimEngine engine;

    Feed(engine, buffer, ":3\n");
    REQUIRE(buffer.Point() == buffer.Content().LineToByteOffset(2));
}

TEST_CASE("Macro recording and playback via qX ... q and @X", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("a\na\na\n");
    VimEngine engine;

    Feed(engine, buffer, "qaA!\x1bjq"); // record: append "!" to line, move down
    REQUIRE(buffer.Text() == "a!\na\na\n");
    Feed(engine, buffer, "@a");
    REQUIRE(buffer.Text() == "a!\na!\na\n");
    Feed(engine, buffer, "@@");
    REQUIRE(buffer.Text() == "a!\na!\na!\n");
}

TEST_CASE("gUU uppercases the current line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("hello world");
    VimEngine engine;

    Feed(engine, buffer, "gUU");
    REQUIRE(buffer.Text() == "HELLO WORLD");
}

TEST_CASE("Marks: ma ... `a jumps back", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abcdefghij");
    VimEngine engine;

    Feed(engine, buffer, "llma");
    Feed(engine, buffer, "$");
    REQUIRE(buffer.Point() == 9);
    Feed(engine, buffer, "`a");
    REQUIRE(buffer.Point() == 2);
}

TEST_CASE("`` toggles between the last two jump positions", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("a\nb\nc\nd\ne\n");
    VimEngine engine;

    Feed(engine, buffer, "G"); // jump to the last line, recording the jump-back mark at 0
    const std::size_t afterG = buffer.Point();
    REQUIRE(afterG != 0);
    Feed(engine, buffer, "``"); // jump back to 0, recording the jump-back mark at afterG
    REQUIRE(buffer.Point() == 0);
    Feed(engine, buffer, "``"); // toggles forward again
    REQUIRE(buffer.Point() == afterG);
}

TEST_CASE("ge moves to the end of the previous word", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abc def");
    VimEngine engine;

    Feed(engine, buffer, "$"); // land on 'f', end of "def"
    REQUIRE(buffer.Point() == 6);
    Feed(engine, buffer, "ge");
    REQUIRE(buffer.Point() == 2); // 'c', end of "abc"
}

TEST_CASE("dge deletes back to the end of the previous word", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abc def");
    VimEngine engine;

    Feed(engine, buffer, "$dge");
    REQUIRE(buffer.Text() == "ab");
}

TEST_CASE("gv reselects the last visual selection", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abcdef");
    VimEngine engine;

    Feed(engine, buffer, "vll\x1b"); // select 'a','b','c' then leave Visual via Escape
    REQUIRE(engine.CurrentMode() == Mode::Normal);
    Feed(engine, buffer, "0$"); // move point elsewhere
    Feed(engine, buffer, "gv");
    REQUIRE(engine.CurrentMode() == Mode::Visual);
    Feed(engine, buffer, "d");
    REQUIRE(buffer.Text() == "def");
}

TEST_CASE(":g/pat/d deletes every matching line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("keep\nDROP\nkeep\nDROP\n");
    VimEngine engine;

    Feed(engine, buffer, ":g/DROP/d\n");
    REQUIRE(buffer.Text() == "keep\nkeep\n");
}

TEST_CASE(":g!/pat/d deletes every non-matching line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("keep\nDROP\nkeep\nDROP\n");
    VimEngine engine;

    Feed(engine, buffer, ":g!/DROP/d\n");
    REQUIRE(buffer.Text() == "DROP\nDROP\n");
}

TEST_CASE(":g/pat/s applies a substitute on every matching line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo one\nbar two\nfoo three\n");
    VimEngine engine;

    Feed(engine, buffer, ":g/foo/s/foo/baz/\n");
    REQUIRE(buffer.Text() == "baz one\nbar two\nbaz three\n");
}

TEST_CASE("Visual block > shifts every touched line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\nthree\n");
    VimEngine engine;

    engine.HandleKey(buffer, CtrlV()); // enter Visual Block at line 0
    Feed(engine, buffer, "j>");        // extend down one line, shift right
    const std::string indent = std::string(static_cast<std::size_t>(ned::editor::TabWidth()), ' ');
    REQUIRE(buffer.Text() == indent + "one\n" + indent + "two\nthree\n"); // third line untouched
}

TEST_CASE("Visual block U uppercases the selected columns only", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abcdef\nghijkl\n");
    VimEngine engine;

    Feed(engine, buffer, "l");         // col 1
    engine.HandleKey(buffer, CtrlV()); // enter Visual Block at (line 0, col 1)
    Feed(engine, buffer, "jlU");       // extend down+right to (line 1, col 2), uppercase
    REQUIRE(buffer.Text() == "aBCdef\ngHIjkl\n");
}

TEST_CASE("~ toggles case of count characters and advances", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abcDEF");
    VimEngine engine;

    Feed(engine, buffer, "~");
    REQUIRE(buffer.Text() == "AbcDEF");
    REQUIRE(buffer.Point() == 1);
    Feed(engine, buffer, "3~");
    REQUIRE(buffer.Text() == "ABCdEF");
    REQUIRE(buffer.Point() == 4);
}

TEST_CASE("~ at end of line does not advance past the last character", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("ab");
    VimEngine engine;

    Feed(engine, buffer, "$~");
    REQUIRE(buffer.Text() == "aB");
    REQUIRE(buffer.Point() == 1); // stays on 'B', doesn't rest past line end
}

TEST_CASE("R enters Replace mode and overtypes characters", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abcdef");
    VimEngine engine;

    Feed(engine, buffer, "RXY");
    REQUIRE(engine.CurrentMode() == Mode::Replace);
    REQUIRE(buffer.Text() == "XYcdef");
    Feed(engine, buffer, "\x1b");
    REQUIRE(engine.CurrentMode() == Mode::Normal);
    REQUIRE(buffer.Point() == 1); // Escape moves back one grapheme, matching Insert's own rule
}

TEST_CASE("Insert-mode C-w deletes the word before point", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar");
    VimEngine engine;

    Feed(engine, buffer, "A"); // point at end of line, Insert mode
    REQUIRE(engine.CurrentMode() == Mode::Insert);
    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U'w')));
    REQUIRE(buffer.Text() == "foo ");
}

TEST_CASE("Insert-mode C-u deletes back to the start of the line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("  indented text");
    VimEngine engine;

    Feed(engine, buffer, "A");
    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U'u')));
    REQUIRE(buffer.Text().empty());
}

TEST_CASE("Insert-mode C-t/C-d indent and outdent the current line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("line");
    VimEngine engine;

    Feed(engine, buffer, "i"); // point at 0, Insert mode
    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U't')));
    const std::string indent = std::string(static_cast<std::size_t>(ned::editor::TabWidth()), ' ');
    REQUIRE(buffer.Text() == indent + "line");
    REQUIRE(buffer.Point() == indent.size());

    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U'd')));
    REQUIRE(buffer.Text() == "line");
    REQUIRE(buffer.Point() == 0);
}

TEST_CASE("Insert-mode C-r inserts a register's contents and stays in Insert mode", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar");
    VimEngine engine;

    Feed(engine, buffer, "\"ayw"); // yank "foo " into register a
    Feed(engine, buffer, "$a");    // enter Insert at end of line
    REQUIRE(engine.CurrentMode() == Mode::Insert);
    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U'r'))); // primes the register-name read
    REQUIRE(engine.HandleInsertModeChord(buffer, Ch(U'a')));   // register name itself
    REQUIRE(engine.CurrentMode() == Mode::Insert);
    REQUIRE(buffer.Text() == "foo barfoo ");
}

TEST_CASE("Insert-mode Ctrl-chords don't leak into ordinary typing/replay", "[VimEngine]") {
    // A chord this codebase's own Emacs bindings would otherwise claim (C-a is
    // beginning-of-line) must never reach the ordinary Dispatcher while a vim Insert
    // session is live -- HandleInsertModeChord returning false is the caller's (real
    // BufferView's) cue to fall through, but a chord with no vim-insert binding at all
    // (like C-a) should still just return false here, unconsumed and unmutating.
    Buffer    buffer = MakeBuffer("text");
    VimEngine engine;

    Feed(engine, buffer, "A");
    REQUIRE_FALSE(engine.HandleInsertModeChord(buffer, Ctrl(U'a')));
    REQUIRE(buffer.Text() == "text");
}

namespace {

Buffer MakeNumberedLineBuffer(std::size_t lineCount) {
    std::string text;
    for (std::size_t i = 0; i < lineCount; ++i) {
        text += "line" + std::to_string(i) + "\n";
    }
    return MakeBuffer(text);
}

std::size_t PointLine(Buffer& buffer) {
    return buffer.Content().ByteOffsetToLine(buffer.Point());
}

} // namespace

TEST_CASE("C-d/C-u scroll point by a half page", "[VimEngine]") {
    Buffer    buffer = MakeNumberedLineBuffer(40);
    VimEngine engine;
    engine.SetViewport(0, 10); // half page == 5 lines

    engine.HandleKey(buffer, Ctrl(U'd'));
    REQUIRE(PointLine(buffer) == 5);

    engine.HandleKey(buffer, Ctrl(U'd'));
    REQUIRE(PointLine(buffer) == 10);

    engine.HandleKey(buffer, Ctrl(U'u'));
    REQUIRE(PointLine(buffer) == 5);
}

TEST_CASE("C-f/C-b scroll point by a full page", "[VimEngine]") {
    Buffer    buffer = MakeNumberedLineBuffer(40);
    VimEngine engine;
    engine.SetViewport(0, 10);

    engine.HandleKey(buffer, Ctrl(U'f'));
    REQUIRE(PointLine(buffer) == 10);

    engine.HandleKey(buffer, Ctrl(U'b'));
    REQUIRE(PointLine(buffer) == 0);
}

TEST_CASE("zz/zt/zb request an explicit topLine_ recenter", "[VimEngine]") {
    Buffer    buffer = MakeNumberedLineBuffer(40);
    VimEngine engine;
    engine.SetViewport(0, 10);
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(20, 0, 1));

    Feed(engine, buffer, "zt");
    REQUIRE(engine.TakePendingTopLine() == std::optional<std::size_t>(20));
    REQUIRE(engine.TakePendingTopLine() == std::nullopt); // one-shot, consumed above

    Feed(engine, buffer, "zz");
    REQUIRE(engine.TakePendingTopLine() == std::optional<std::size_t>(15)); // 20 - height/2

    Feed(engine, buffer, "zb");
    REQUIRE(engine.TakePendingTopLine() == std::optional<std::size_t>(11)); // 20 - height + 1
}

TEST_CASE("C-e/C-y scroll the viewport without moving point", "[VimEngine]") {
    Buffer    buffer = MakeNumberedLineBuffer(40);
    VimEngine engine;
    engine.SetViewport(5, 10);
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(7, 0, 1));

    engine.HandleKey(buffer, Ctrl(U'e'));
    REQUIRE(engine.TakePendingTopLine() == std::optional<std::size_t>(6));
    REQUIRE(PointLine(buffer) == 7); // point untouched

    engine.SetViewport(5, 10);
    engine.HandleKey(buffer, Ctrl(U'y'));
    REQUIRE(engine.TakePendingTopLine() == std::optional<std::size_t>(4));
    REQUIRE(PointLine(buffer) == 7);
}

TEST_CASE("ZZ saves and requests CloseBuffer", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("content\n");
    VimEngine engine;
    buffer.SetPath(std::filesystem::temp_directory_path() / "ned_vimengine_test_zz.txt");

    Feed(engine, buffer, "ZZ");
    REQUIRE(engine.TakePendingIntent() == PendingIntent::CloseBuffer);
    REQUIRE_FALSE(buffer.Modified());
    std::filesystem::remove(*buffer.Path());
}

TEST_CASE("ZQ requests CloseBuffer without saving", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("content\n");
    VimEngine engine;

    Feed(engine, buffer, "ZQ");
    REQUIRE(engine.TakePendingIntent() == PendingIntent::CloseBuffer);
}

TEST_CASE("gJ joins without inserting a space", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo\nbar\n");
    VimEngine engine;

    Feed(engine, buffer, "g");
    engine.HandleKey(buffer, Ch(U'J'));
    REQUIRE(buffer.Text() == "foobar\n");
}

TEST_CASE("gi resumes Insert where it was last exited", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abcdef");
    VimEngine engine;

    Feed(engine, buffer, "llli"); // point at 3, enter Insert
    REQUIRE(engine.CurrentMode() == Mode::Insert);
    Feed(engine, buffer, "XYZ");
    Feed(engine, buffer, "\x1b"); // back to Normal, point moves back one grapheme
    REQUIRE(engine.CurrentMode() == Mode::Normal);

    Feed(engine, buffer, "0"); // move point away
    Feed(engine, buffer, "g");
    engine.HandleKey(buffer, Ch(U'i'));
    REQUIRE(engine.CurrentMode() == Mode::Insert);
    REQUIRE(buffer.Point() == 6); // right after "XYZ", where Insert was left
}

TEST_CASE("C-a increments the number under/after point", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("count: 41");
    VimEngine engine;

    engine.HandleKey(buffer, Ctrl(U'a'));
    REQUIRE(buffer.Text() == "count: 42");
}

TEST_CASE("C-x decrements the number under/after point", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("count: 41");
    VimEngine engine;

    engine.HandleKey(buffer, Ctrl(U'x'));
    REQUIRE(buffer.Text() == "count: 40");
}

TEST_CASE("C-a/C-x preserve zero-padded width and handle sign crossing", "[VimEngine]") {
    Buffer    buffer1 = MakeBuffer("id 007");
    VimEngine engine1;
    engine1.HandleKey(buffer1, Ctrl(U'a'));
    REQUIRE(buffer1.Text() == "id 008");

    Buffer    buffer2 = MakeBuffer("x = -3");
    VimEngine engine2;
    engine2.HandleKey(buffer2, Ctrl(U'a'));
    REQUIRE(buffer2.Text() == "x = -2");

    Buffer    buffer3 = MakeBuffer("y = 2");
    VimEngine engine3;
    Feed(engine3, buffer3, "5"); // count = 5
    engine3.HandleKey(buffer3, Ctrl(U'x'));
    REQUIRE(buffer3.Text() == "y = -3"); // crosses zero, sign gets added
}

TEST_CASE("count applies to C-a/C-x", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("n=10");
    VimEngine engine;

    Feed(engine, buffer, "5");
    engine.HandleKey(buffer, Ctrl(U'a'));
    REQUIRE(buffer.Text() == "n=15");
}

TEST_CASE(":j joins a range of lines", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\nthree\n");
    VimEngine engine;

    Feed(engine, buffer, ":1,2j\n");
    REQUIRE(buffer.Text() == "one two\nthree\n");
}

TEST_CASE(":y yanks a range into a named register", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo\nbar\n");
    VimEngine engine;

    Feed(engine, buffer, ":y a\n");
    Feed(engine, buffer, "\"ap");
    REQUIRE(buffer.Text() == "foo\nfoo\nbar\n");
}

TEST_CASE(":pu pastes the unnamed register as lines after the target", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo\nbar\n");
    VimEngine engine;

    Feed(engine, buffer, "yy"); // yank "foo" into the unnamed register
    Feed(engine, buffer, "j");  // move to "bar"
    Feed(engine, buffer, ":pu\n");
    REQUIRE(buffer.Text() == "foo\nbar\nfoo\n");
}

TEST_CASE(":put! pastes before the target line", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo\nbar\n");
    VimEngine engine;

    Feed(engine, buffer, "yy");
    Feed(engine, buffer, "j");
    Feed(engine, buffer, ":put!\n");
    REQUIRE(buffer.Text() == "foo\nfoo\nbar\n");
}

TEST_CASE(":> and :< shift a range of lines", "[VimEngine]") {
    Buffer            buffer = MakeBuffer("a\nb\nc\n");
    VimEngine         engine;
    const std::string indent = std::string(static_cast<std::size_t>(ned::editor::TabWidth()), ' ');

    Feed(engine, buffer, ":2>\n");
    REQUIRE(buffer.Text() == "a\n" + indent + "b\nc\n");

    Feed(engine, buffer, ":2<\n");
    REQUIRE(buffer.Text() == "a\nb\nc\n");
}

TEST_CASE(":m moves a line to after the destination address", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\nthree\n");
    VimEngine engine;

    Feed(engine, buffer, ":1m$\n");
    REQUIRE(buffer.Text() == "two\nthree\none\n");
}

TEST_CASE(":t/:copy duplicates a line after the destination address", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("one\ntwo\nthree\n");
    VimEngine engine;

    Feed(engine, buffer, ":1t$\n");
    REQUIRE(buffer.Text() == "one\ntwo\nthree\none\n");
}

TEST_CASE(":sort sorts lines lexicographically, :sort! reverses", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("banana\napple\ncherry\n");
    VimEngine engine;

    Feed(engine, buffer, ":sort\n");
    REQUIRE(buffer.Text() == "apple\nbanana\ncherry\n");

    Feed(engine, buffer, ":sort!\n");
    REQUIRE(buffer.Text() == "cherry\nbanana\napple\n");
}

TEST_CASE(":r reads a file's contents in after the target line", "[VimEngine]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_vimengine_test_read.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << "inserted\n";
    }
    Buffer    buffer = MakeBuffer("one\ntwo\n");
    VimEngine engine;

    Feed(engine, buffer, ":r " + path.string() + "\n");
    REQUIRE(buffer.Text() == "one\ninserted\ntwo\n");
    std::filesystem::remove(path);
}

TEST_CASE("& repeats the last :s on the current line only", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo\nfoo\nfoo\n");
    VimEngine engine;

    Feed(engine, buffer, ":s/foo/bar/\n");
    REQUIRE(buffer.Text() == "bar\nfoo\nfoo\n");

    Feed(engine, buffer, "j&");
    REQUIRE(buffer.Text() == "bar\nbar\nfoo\n");

    Feed(engine, buffer, "j&");
    REQUIRE(buffer.Text() == "bar\nbar\nbar\n");
}

TEST_CASE("dis deletes just the sentence, leaving surrounding whitespace intact", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("One. Two. Three.");
    VimEngine engine;

    buffer.SetPoint(6); // inside "Two."
    Feed(engine, buffer, "dis");
    REQUIRE(buffer.Text() == "One.  Three."); // both the space before and the one after remain
}

TEST_CASE("das also deletes the sentence's own trailing whitespace", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("One. Two. Three.");
    VimEngine engine;

    buffer.SetPoint(6);
    Feed(engine, buffer, "das");
    REQUIRE(buffer.Text() == "One. Three.");
    // "das" additionally removes the space between "One." and "Two." was; disambiguate
    // from dis by checking there's no leftover double space where "Two. " sat.
    REQUIRE(buffer.Text().find("  ") == std::string::npos);
}

TEST_CASE("dit/dat delete tag content, with/without the tags themselves", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("<div>hello</div>");
    VimEngine engine;

    buffer.SetPoint(7); // inside "hello"
    Feed(engine, buffer, "dit");
    REQUIRE(buffer.Text() == "<div></div>");

    Feed(engine, buffer, "u"); // undo, then try dat
    REQUIRE(buffer.Text() == "<div>hello</div>");
    buffer.SetPoint(7);
    Feed(engine, buffer, "dat");
    REQUIRE(buffer.Text().empty());
}

TEST_CASE("2diw deletes two words (a word plus the whitespace/word run after it)", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar baz");
    VimEngine engine;

    Feed(engine, buffer, "2diw");
    REQUIRE(buffer.Text() == "bar baz");
}

TEST_CASE("d2iw behaves the same as 2diw", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar baz");
    VimEngine engine;

    Feed(engine, buffer, "d2iw");
    REQUIRE(buffer.Text() == "bar baz");
}

TEST_CASE("The / register holds the last search pattern", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("hello world");
    VimEngine engine;

    Feed(engine, buffer, "/world\n");
    Feed(engine, buffer, "0\"/p");
    REQUIRE(buffer.Text() == "hworldello world");
}

TEST_CASE("The : register holds the last ex command's raw text", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("hello");
    VimEngine engine;

    Feed(engine, buffer, ":s/hello/hi/\n");
    REQUIRE(buffer.Text() == "hi");
    Feed(engine, buffer, "\":p");
    REQUIRE(buffer.Text() == "hs/hello/hi/i");
}

TEST_CASE("The . register holds the last inserted text", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("world");
    VimEngine engine;

    Feed(engine, buffer, "iHello\x1b");
    REQUIRE(buffer.Text() == "Helloworld");
    Feed(engine, buffer, "$\".p");
    REQUIRE(buffer.Text() == "HelloworldHello");
}

TEST_CASE("The % register holds the buffer's own file path", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("x");
    VimEngine engine;
    buffer.SetPath("/tmp/ned_vimengine_test_percent.txt");

    Feed(engine, buffer, "\"%p");
    REQUIRE(buffer.Text() == "x/tmp/ned_vimengine_test_percent.txt");
}

TEST_CASE("Special registers fall through to ordinary named storage for other names", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar");
    VimEngine engine;

    Feed(engine, buffer, "\"ayw"); // yank "foo " into register a
    Feed(engine, buffer, "$\"ap");
    REQUIRE(buffer.Text() == "foo barfoo ");
}

TEST_CASE("Insert-mode C-o executes one Normal command then resumes Insert", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("abc");
    VimEngine engine;

    Feed(engine, buffer, "A"); // point at end (3), Insert mode
    engine.RecordInsertKey(Ctrl(U'o'));
    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U'o')));
    REQUIRE(engine.CurrentMode() == Mode::Normal);

    engine.HandleKey(buffer, Ch(U'0')); // the one Normal command: move to line start
    REQUIRE(engine.CurrentMode() == Mode::Insert);
    REQUIRE(buffer.Point() == 0);
}

TEST_CASE("Insert-mode C-o supports a full operator+motion before resuming", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar");
    VimEngine engine;

    Feed(engine, buffer, "i"); // Insert mode at point 0
    engine.RecordInsertKey(Ctrl(U'o'));
    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U'o')));
    REQUIRE(engine.CurrentMode() == Mode::Normal);

    engine.HandleKey(buffer, Ch(U'd'));
    REQUIRE(engine.CurrentMode() == Mode::Normal); // operator pending, not yet resolved
    engine.HandleKey(buffer, Ch(U'w'));
    REQUIRE(engine.CurrentMode() == Mode::Insert); // motion completed the operator, now resumed
    REQUIRE(buffer.Text() == "bar");
}

TEST_CASE("Insert-mode C-o followed by a mode-entering command doesn't corrupt later commands", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("ab\ncd\n");
    VimEngine engine;

    Feed(engine, buffer, "i"); // Insert at point 0 on line "ab"
    engine.RecordInsertKey(Ctrl(U'o'));
    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U'o')));
    engine.HandleKey(buffer, Ch(U'A')); // one-shot command itself re-enters Insert at EOL
    REQUIRE(engine.CurrentMode() == Mode::Insert);

    Feed(engine, buffer, "X\x1b"); // finish this insert session normally
    REQUIRE(buffer.Text() == "abX\ncd\n");

    // A later, unrelated Normal-mode command must behave normally -- not silently jump
    // back into Insert mode because of a stale oneShotNormalPending_ flag.
    Feed(engine, buffer, "j");
    REQUIRE(engine.CurrentMode() == Mode::Normal);
}

TEST_CASE("Dot-repeat replays an Insert session that used C-o", "[VimEngine]") {
    Buffer    buffer = MakeBuffer("foo bar\nfoo bar\n");
    VimEngine engine;

    Feed(engine, buffer, "i");
    engine.RecordInsertKey(Ctrl(U'o'));
    REQUIRE(engine.HandleInsertModeChord(buffer, Ctrl(U'o')));
    engine.HandleKey(buffer, Ch(U'd'));
    engine.HandleKey(buffer, Ch(U'w')); // deletes "foo " via the one-shot excursion, resumes Insert
    Feed(engine, buffer, "X\x1b");      // types "X" then exits Insert

    REQUIRE(buffer.Text() == "Xbar\nfoo bar\n");

    Feed(engine, buffer, "j0."); // move to line 2, repeat the whole recorded change
    REQUIRE(buffer.Text() == "Xbar\nXbar\n");
}
