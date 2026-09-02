#include <catch2/catch_test_macros.hpp>

#include "Editor/Vim/VimSurround.h"
#include "Text/Buffer.h"

using ned::editor::vim::AddSurround;
using ned::editor::vim::ChangeSurroundAtPoint;
using ned::editor::vim::DeleteSurroundAtPoint;
using ned::editor::vim::ResolveSurroundTarget;
using ned::text::Buffer;

namespace {
Buffer MakeBuffer(const std::string& text) {
    Buffer buffer("test");
    buffer.InsertAtPoint(text);
    buffer.SetPoint(0);
    return buffer;
}
} // namespace

TEST_CASE("ResolveSurroundTarget pads an opening bracket char but not its closing/alias counterpart", "[VimSurround]") {
    const auto open = ResolveSurroundTarget(U'(');
    REQUIRE(open);
    REQUIRE(open->open == "( ");
    REQUIRE(open->close == " )");

    const auto close = ResolveSurroundTarget(U')');
    REQUIRE(close);
    REQUIRE(close->open == "(");
    REQUIRE(close->close == ")");

    const auto alias = ResolveSurroundTarget(U'b');
    REQUIRE(alias);
    REQUIRE(alias->open == "(");
    REQUIRE(alias->close == ")");

    const auto quote = ResolveSurroundTarget(U'"');
    REQUIRE(quote);
    REQUIRE(quote->open == "\"");
    REQUIRE(quote->close == "\"");

    REQUIRE_FALSE(ResolveSurroundTarget(U'z'));
}

TEST_CASE("AddSurround wraps a range and reports its start as the new point", "[VimSurround]") {
    Buffer buffer = MakeBuffer("hello world");

    std::size_t point = 0;
    REQUIRE(AddSurround(buffer, 0, 5, U'"', point));
    REQUIRE(buffer.Text() == "\"hello\" world");
    REQUIRE(point == 0);
}

TEST_CASE("AddSurround fails without touching the buffer for an unresolved target", "[VimSurround]") {
    Buffer buffer = MakeBuffer("hello");

    std::size_t point = 99;
    REQUIRE_FALSE(AddSurround(buffer, 0, 5, U'z', point));
    REQUIRE(buffer.Text() == "hello");
}

TEST_CASE("DeleteSurroundAtPoint strips the enclosing quotes", "[VimSurround]") {
    Buffer buffer = MakeBuffer("x = \"hello\" + 1");
    buffer.SetPoint(6); // inside "hello"

    REQUIRE(DeleteSurroundAtPoint(buffer, U'"'));
    REQUIRE(buffer.Text() == "x = hello + 1");
}

TEST_CASE("DeleteSurroundAtPoint strips the enclosing brackets, aliasable via b", "[VimSurround]") {
    Buffer buffer = MakeBuffer("foo(bar(baz)qux)end");
    buffer.SetPoint(9); // inside the inner "baz" parens

    REQUIRE(DeleteSurroundAtPoint(buffer, U'b'));
    REQUIRE(buffer.Text() == "foo(barbazqux)end");
}

TEST_CASE("DeleteSurroundAtPoint strips an enclosing tag", "[VimSurround]") {
    Buffer buffer = MakeBuffer("<div><span>hi</span></div>");
    buffer.SetPoint(13); // inside "hi"

    REQUIRE(DeleteSurroundAtPoint(buffer, U't'));
    REQUIRE(buffer.Text() == "<div>hi</div>");
}

TEST_CASE("DeleteSurroundAtPoint is a no-op when no such pair encloses point", "[VimSurround]") {
    Buffer buffer = MakeBuffer("hello world");
    buffer.SetPoint(3);

    REQUIRE_FALSE(DeleteSurroundAtPoint(buffer, U'"'));
    REQUIRE(buffer.Text() == "hello world");
}

TEST_CASE("ChangeSurroundAtPoint swaps double quotes for single quotes", "[VimSurround]") {
    Buffer buffer = MakeBuffer("say \"hi\" now");
    buffer.SetPoint(6);

    REQUIRE(ChangeSurroundAtPoint(buffer, U'"', U'\''));
    REQUIRE(buffer.Text() == "say 'hi' now");
}

TEST_CASE("ChangeSurroundAtPoint from quotes to a padded bracket", "[VimSurround]") {
    Buffer buffer = MakeBuffer("say \"hi\" now");
    buffer.SetPoint(6);

    REQUIRE(ChangeSurroundAtPoint(buffer, U'"', U'('));
    REQUIRE(buffer.Text() == "say ( hi ) now");
}

TEST_CASE("ChangeSurroundAtPoint fails without touching the buffer for an unresolved target", "[VimSurround]") {
    Buffer buffer = MakeBuffer("say \"hi\" now");
    buffer.SetPoint(6);

    REQUIRE_FALSE(ChangeSurroundAtPoint(buffer, U'"', U'z'));
    REQUIRE(buffer.Text() == "say \"hi\" now");
}
