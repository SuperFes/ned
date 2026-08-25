#include <catch2/catch_test_macros.hpp>

#include "Editor/Vim/VimTextObject.h"
#include "Text/Buffer.h"

using ned::editor::vim::AroundBracket;
using ned::editor::vim::AroundParagraph;
using ned::editor::vim::AroundQuote;
using ned::editor::vim::AroundSentence;
using ned::editor::vim::AroundTag;
using ned::editor::vim::AroundWord;
using ned::editor::vim::InnerBracket;
using ned::editor::vim::InnerParagraph;
using ned::editor::vim::InnerQuote;
using ned::editor::vim::InnerSentence;
using ned::editor::vim::InnerTag;
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

TEST_CASE("count on iw/aw extends by additional word/whitespace runs", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("foo bar baz");

    const auto iw2 = InnerWord(buffer, 0, false, 2); // "foo" then the space run after it
    REQUIRE(buffer.Content().Substring(iw2.start, iw2.end - iw2.start) == "foo ");

    const auto iw3 = InnerWord(buffer, 0, false, 3); // "foo" + space + "bar"
    REQUIRE(buffer.Content().Substring(iw3.start, iw3.end - iw3.start) == "foo bar");

    const auto aw3 = AroundWord(buffer, 0, false, 3); // iw3 plus the trailing space after "bar"
    REQUIRE(buffer.Content().Substring(aw3.start, aw3.end - aw3.start) == "foo bar ");
}

TEST_CASE("is/as select a sentence, with/without its trailing whitespace", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("One. Two. Three.");

    const auto inner = InnerSentence(buffer, 6); // inside "Two."
    REQUIRE(inner.found);
    REQUIRE(buffer.Content().Substring(inner.start, inner.end - inner.start) == "Two.");

    const auto around = AroundSentence(buffer, 6);
    REQUIRE(buffer.Content().Substring(around.start, around.end - around.start) == "Two. ");
}

TEST_CASE("is at a sentence's own first character still selects that sentence", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("One. Two. Three.");

    const auto inner = InnerSentence(buffer, 5); // exactly the 'T' of "Two."
    REQUIRE(buffer.Content().Substring(inner.start, inner.end - inner.start) == "Two.");
}

TEST_CASE("2is extends the sentence selection by one more sentence", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("One. Two. Three.");

    const auto inner = InnerSentence(buffer, 6, 2); // "Two." + "Three."
    REQUIRE(buffer.Content().Substring(inner.start, inner.end - inner.start) == "Two. Three.");
}

TEST_CASE("it/at select tag content and the tags themselves", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("<div>hello</div>");

    const auto inner = InnerTag(buffer, 7); // inside "hello"
    REQUIRE(inner.found);
    REQUIRE(buffer.Content().Substring(inner.start, inner.end - inner.start) == "hello");

    const auto around = AroundTag(buffer, 7);
    REQUIRE(buffer.Content().Substring(around.start, around.end - around.start) == "<div>hello</div>");
}

TEST_CASE("it/at resolve the innermost enclosing tag when nested", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("<div><span>hi</span></div>");

    const auto innerSpan = InnerTag(buffer, 12); // inside "hi"
    REQUIRE(buffer.Content().Substring(innerSpan.start, innerSpan.end - innerSpan.start) == "hi");

    const auto aroundSpan = AroundTag(buffer, 12);
    REQUIRE(buffer.Content().Substring(aroundSpan.start, aroundSpan.end - aroundSpan.start) == "<span>hi</span>");
}

TEST_CASE("it/at skip self-closing tags entirely", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("<div>before<br/>after</div>");

    const auto inner = InnerTag(buffer, 18); // inside "after", past the self-closing <br/>
    REQUIRE(buffer.Content().Substring(inner.start, inner.end - inner.start) == "before<br/>after");
}

TEST_CASE("it/at report not found outside any tag", "[VimTextObject]") {
    Buffer buffer = MakeBuffer("no tags here");

    REQUIRE_FALSE(InnerTag(buffer, 3).found);
}
