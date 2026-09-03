#include <catch2/catch_test_macros.hpp>

#include "Editor/Lsp/LspPosition.h"
#include "Text/Rope.h"
#include "Text/RopeStorage.h"

using ned::editor::lsp::BytePositionToLsp;
using ned::editor::lsp::ByteRangeToLspRange;
using ned::editor::lsp::LspPosition;
using ned::editor::lsp::LspPositionToByte;
using ned::editor::lsp::LspRange;
using ned::editor::lsp::Utf16LengthOfByteRange;
using ned::text::Rope;
using ned::text::RopeStorage;

TEST_CASE("BytePositionToLsp/LspPositionToByte round-trip over plain ASCII", "[Lsp]") {
    const RopeStorage content(Rope("first line\nsecond line\nthird"));

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
    const RopeStorage content(Rope("caf\xc3\xa9!"));

    const std::size_t byteOffsetOfBang = 5; // 'c','a','f','é'(2 bytes) = 5 bytes in
    const LspPosition position         = BytePositionToLsp(content, byteOffsetOfBang);
    REQUIRE(position.line == 0);
    REQUIRE(position.character == 4); // c, a, f, é -- 4 UTF-16 code units
    REQUIRE(LspPositionToByte(content, position) == byteOffsetOfBang);
}

TEST_CASE("BytePositionToLsp counts a supplementary-plane codepoint as two UTF-16 code units (a surrogate pair)", "[Lsp]") {
    // U+1F600 (grinning face emoji), 4 bytes in UTF-8, a surrogate pair (2 code units) in UTF-16.
    const RopeStorage content(Rope("\xF0\x9F\x98\x80X"));

    const std::size_t byteOffsetOfX = 4;
    const LspPosition position      = BytePositionToLsp(content, byteOffsetOfX);
    REQUIRE(position.line == 0);
    REQUIRE(position.character == 2); // one emoji codepoint = 2 UTF-16 code units
    REQUIRE(LspPositionToByte(content, position) == byteOffsetOfX);
}

TEST_CASE("LspPositionToByte is bounded by the line's own byte range for an out-of-range character", "[Lsp]") {
    const RopeStorage content(Rope("ab\ncd"));

    // Line 0's own byte range is [0, 3) -- "ab" plus its trailing newline
    // (the next line's own start byte is the exclusive bound, not the
    // newline-excluded content length); asking for character 999 must not
    // walk past that bound.
    const std::size_t byteOffset = LspPositionToByte(content, LspPosition{.line = 0, .character = 999});
    REQUIRE(byteOffset == 3);
}

TEST_CASE("ByteRangeToLspRange computes a single-line ASCII range", "[Lsp]") {
    const std::string content = "abcdef";
    const LspRange    range   = ByteRangeToLspRange(content, 2, 5); // "cde"
    REQUIRE(range.start == LspPosition{.line = 0, .character = 2});
    REQUIRE(range.end == LspPosition{.line = 0, .character = 5});
}

TEST_CASE("ByteRangeToLspRange spans multiple lines", "[Lsp]") {
    const std::string content = "first\nsecond\nthird";
    // "d\nt" -- the last byte of "second", the newline, and the first byte
    // of "third": a span crossing exactly one line boundary.
    const std::size_t startByte = content.find("d\n"); // 'd' of "second"
    const std::size_t endByte   = startByte + 3;       // spans "d\nt"
    const LspRange    range     = ByteRangeToLspRange(content, startByte, endByte);
    REQUIRE(range.start == LspPosition{.line = 1, .character = 5}); // "secon|d"
    REQUIRE(range.end == LspPosition{.line = 2, .character = 1});   // "t|hird"
}

TEST_CASE("ByteRangeToLspRange counts a non-BMP codepoint as two UTF-16 units", "[Lsp]") {
    // U+1F600 (grinning face emoji), 4 bytes in UTF-8, a surrogate pair (2 code units) in UTF-16.
    const std::string content = "\xF0\x9F\x98\x80X";
    const LspRange    range   = ByteRangeToLspRange(content, 0, 4); // just the emoji
    REQUIRE(range.start == LspPosition{.line = 0, .character = 0});
    REQUIRE(range.end == LspPosition{.line = 0, .character = 2});
}

TEST_CASE("ByteRangeToLspRange handles startByte == endByte as a zero-width range", "[Lsp]") {
    const std::string content = "abcdef";
    const LspRange    range   = ByteRangeToLspRange(content, 3, 3);
    REQUIRE(range.start == range.end);
    REQUIRE(range.start == LspPosition{.line = 0, .character = 3});
}

TEST_CASE("Utf16LengthOfByteRange matches the character delta ByteRangeToLspRange reports for the same span", "[Lsp]") {
    const std::string content     = "caf\xc3\xa9 \xF0\x9F\x98\x80!"; // "café <emoji>!"
    const std::size_t startByte   = 0;
    const std::size_t endByte     = content.size() - 1; // up to, not including, the trailing '!'
    const LspRange    range       = ByteRangeToLspRange(content, startByte, endByte);
    const std::size_t utf16Length = Utf16LengthOfByteRange(content, startByte, endByte);
    REQUIRE(range.end.character - range.start.character == utf16Length);
}
