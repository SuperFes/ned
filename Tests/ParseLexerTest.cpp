#include <catch2/catch_test_macros.hpp>

#include "Editor/Parse/Lexer.h"

using ned::editor::parse::Length;
using ned::editor::parse::Lexer;
using ned::editor::parse::Range;

namespace {

std::int32_t LookbehindAt(Lexer& lexer, std::uint32_t byte) {
    lexer.Reset(Length{byte, {0, byte}});
    lexer.Start();
    return lexer.data.lookbehind(&lexer.data);
}

} // namespace

TEST_CASE("A scanner can read the codepoint before its position", "[ParseLexer]") {
    Lexer lexer;
    lexer.SetText("a\xC3\xA9*b");
    CHECK(LookbehindAt(lexer, 0) == '\n');
    CHECK(LookbehindAt(lexer, 1) == 'a');
    CHECK(LookbehindAt(lexer, 3) == 0xE9);
    CHECK(LookbehindAt(lexer, 4) == '*');
}

TEST_CASE("Lookbehind treats an included range's start as a line start", "[ParseLexer]") {
    Lexer       lexer;
    const Range ranges[] = {
        {.startPoint = {0, 0}, .endPoint = {0, 3}, .startByte = 0, .endByte = 3},
        {.startPoint = {1, 2}, .endPoint = {1, 4}, .startByte = 5, .endByte = 7},
    };
    lexer.SetText("ab\n> cd");
    REQUIRE(lexer.SetIncludedRanges(ranges, 2));
    CHECK(LookbehindAt(lexer, 5) == '\n');
    CHECK(LookbehindAt(lexer, 6) == 'c');
}
