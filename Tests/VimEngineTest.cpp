#include <catch2/catch_test_macros.hpp>

#include "Editor/Vim/VimEngine.h"
#include "Text/Buffer.h"

using ned::editor::KeyChord;
using ned::editor::SpecialKey;
using ned::editor::vim::Mode;
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
