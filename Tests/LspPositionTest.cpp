#include <catch2/catch_test_macros.hpp>

#include "Editor/Lsp/LspPosition.h"
#include "Text/Rope.h"

using ned::editor::lsp::BytePositionToLsp;
using ned::editor::lsp::LspPosition;
using ned::editor::lsp::LspPositionToByte;
using ned::text::Rope;

TEST_CASE("BytePositionToLsp/LspPositionToByte round-trip over plain ASCII", "[Lsp]") {
    const Rope content("first line\nsecond line\nthird");

    // Byte offset of 's' in "second" -- line 1, column 0.
    const std::size_t byteOffset = content.LineToByteOffset(1);
    const LspPosition position  = BytePositionToLsp(content, byteOffset);
    REQUIRE(position.line == 1);
    REQUIRE(position.character == 0);
    REQUIRE(LspPositionToByte(content, position) == byteOffset);

    // Somewhere mid-line: "second" -> after "sec" is +3 columns/bytes.
    const std::size_t midOffset   = byteOffset + 3;
    const LspPosition midPosition = BytePositionToLsp(content, midOffset);
    REQUIRE(midPosition.line == 1);
    REQUIRE(midPosition.character == 3);
    REQUIRE(LspPositionToByte(content, midPosition) == midOffset);
}

TEST_CASE("BytePositionToLsp counts a multi-byte UTF-8 codepoint as one UTF-16 code unit", "[Lsp]") {
    // "café" -- 'é' is U+00E9, 2 bytes in UTF-8, 1 code unit in UTF-16.
    const Rope content("caf\xc3\xa9!");

    const std::size_t byteOffsetOfBang = 5; // 'c','a','f','é'(2 bytes) = 5 bytes in
    const LspPosition position         = BytePositionToLsp(content, byteOffsetOfBang);
    REQUIRE(position.line == 0);
    REQUIRE(position.character == 4); // c, a, f, é -- 4 UTF-16 code units
    REQUIRE(LspPositionToByte(content, position) == byteOffsetOfBang);
}

TEST_CASE("BytePositionToLsp counts a supplementary-plane codepoint as two UTF-16 code units (a surrogate pair)", "[Lsp]") {
    // U+1F600 (grinning face emoji), 4 bytes in UTF-8, a surrogate pair (2 code units) in UTF-16.
    const Rope content("\xF0\x9F\x98\x80X");

    const std::size_t byteOffsetOfX = 4;
    const LspPosition position      = BytePositionToLsp(content, byteOffsetOfX);
    REQUIRE(position.line == 0);
    REQUIRE(position.character == 2); // one emoji codepoint = 2 UTF-16 code units
    REQUIRE(LspPositionToByte(content, position) == byteOffsetOfX);
}

TEST_CASE("LspPositionToByte is bounded by the line's own byte range for an out-of-range character", "[Lsp]") {
    const Rope content("ab\ncd");

    // Line 0's own byte range is [0, 3) -- "ab" plus its trailing newline
    // (the next line's own start byte is the exclusive bound, not the
    // newline-excluded content length); asking for character 999 must not
    // walk past that bound.
    const std::size_t byteOffset = LspPositionToByte(content, LspPosition{.line = 0, .character = 999});
    REQUIRE(byteOffset == 3);
}
