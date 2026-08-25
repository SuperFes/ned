#include <catch2/catch_test_macros.hpp>

#include "Editor/Vim/VimTextObject.h"
#include "Text/Buffer.h"

using ned::editor::vim::AroundBracket;
using ned::editor::vim::AroundParagraph;
using ned::editor::vim::AroundQuote;
using ned::editor::vim::AroundWord;
using ned::editor::vim::InnerBracket;
using ned::editor::vim::InnerParagraph;
using ned::editor::vim::InnerQuote;
using ned::editor::vim::InnerWord;
using ned::text::Buffer;

namespace {
Buffer MakeBuffer(const std::string& text) {
    Buffer buffer("test");
    buffer.InsertAtPoint(text);
    buffer.SetPoint(0);
    return buffer;
}
} // namespace

TEST_CASE("iw/aw select a word and its surrounding whitespace", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("foo bar baz");

    const auto iw = InnerWord(buffer, 5, false); // inside "bar"
    REQUIRE(iw.found);
    REQUIRE(buffer.Content().Substring(iw.start, iw.end - iw.start) == "bar");

    const auto aw = AroundWord(buffer, 5, false);
    REQUIRE(aw.found);
    REQUIRE(buffer.Content().Substring(aw.start, aw.end - aw.start) == "bar "); // trailing space eaten
}

TEST_CASE("aw eats leading whitespace when there's no trailing whitespace", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("foo bar");

    const auto aw = AroundWord(buffer, 5, false); // inside "bar", last word, no trailing space
    REQUIRE(buffer.Content().Substring(aw.start, aw.end - aw.start) == " bar");
}

TEST_CASE("i\"/a\" select quoted content and the quotes themselves", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("x = \"hello\" + 1");

    const auto inner = InnerQuote(buffer, 6, U'"');
    REQUIRE(inner.found);
    REQUIRE(buffer.Content().Substring(inner.start, inner.end - inner.start) == "hello");

    const auto around = AroundQuote(buffer, 6, U'"');
    REQUIRE(buffer.Content().Substring(around.start, around.end - around.start) == "\"hello\"");
}

TEST_CASE("i(/a( work from inside, and from resting on either bracket", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("foo(bar(baz)qux)end");

    const auto innerOuter = InnerBracket(buffer, 5, U'(', U')'); // inside "bar(baz)qux", on the 'b' of bar
    REQUIRE(innerOuter.found);
    REQUIRE(buffer.Content().Substring(innerOuter.start, innerOuter.end - innerOuter.start) == "bar(baz)qux");

    const auto innerNested = InnerBracket(buffer, 9, U'(', U')'); // inside "baz"
    REQUIRE(buffer.Content().Substring(innerNested.start, innerNested.end - innerNested.start) == "baz");

    const auto onOpen = InnerBracket(buffer, 3, U'(', U')'); // resting on the outer '('
    REQUIRE(buffer.Content().Substring(onOpen.start, onOpen.end - onOpen.start) == "bar(baz)qux");

    const auto onClose = InnerBracket(buffer, 15, U'(', U')'); // resting on the outer ')'
    REQUIRE(buffer.Content().Substring(onClose.start, onClose.end - onClose.start) == "bar(baz)qux");

    const auto around = AroundBracket(buffer, 5, U'(', U')');
    REQUIRE(buffer.Content().Substring(around.start, around.end - around.start) == "(bar(baz)qux)");
}

TEST_CASE("i(/a( report not found outside any bracket pair", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("no brackets here");

    REQUIRE_FALSE(InnerBracket(buffer, 3, U'(', U')').found);
}

TEST_CASE("ip/ap select a contiguous non-blank paragraph and its trailing blank run", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("a\nb\n\nc\nd\n\ne\n");

    const auto inner = InnerParagraph(buffer, 0); // "a\nb\n"
    REQUIRE(inner.linewise);
    REQUIRE(buffer.Content().Substring(inner.start, inner.end - inner.start) == "a\nb\n");

    const auto around = AroundParagraph(buffer, 0); // "a\nb\n" plus the blank line after it
    REQUIRE(buffer.Content().Substring(around.start, around.end - around.start) == "a\nb\n\n");
}
