#include <catch2/catch_test_macros.hpp>

#include "Text/Utf8.h"

using ned::text::EncodeCodepointUtf8;

TEST_CASE("EncodeCodepointUtf8 encodes ASCII as one byte", "[Utf8]") {
    REQUIRE(EncodeCodepointUtf8(U'a') == "a");
    REQUIRE(EncodeCodepointUtf8(U'\0') == std::string(1, '\0'));
}

TEST_CASE("EncodeCodepointUtf8 encodes multi-byte codepoints correctly", "[Utf8]") {
    REQUIRE(EncodeCodepointUtf8(0x00E9) == "\xC3\xA9");          // 'é', 2 bytes
    REQUIRE(EncodeCodepointUtf8(0x4E2D) == "\xE4\xB8\xAD");      // '中', 3 bytes
    REQUIRE(EncodeCodepointUtf8(0x1F600) == "\xF0\x9F\x98\x80"); // grinning face emoji, 4 bytes
}

TEST_CASE("NextCodepointBoundary steps one codepoint at a time", "[Utf8]") {
    using ned::text::NextCodepointBoundary;

    const std::string text = "a\xC3\xA9\xE4\xB8\xAD"; // 'a' + 'é' + '中'
    REQUIRE(NextCodepointBoundary(text, 0) == 1);
    REQUIRE(NextCodepointBoundary(text, 1) == 3);
    REQUIRE(NextCodepointBoundary(text, 3) == 6);
    REQUIRE(NextCodepointBoundary(text, 6) == 6);  // at the end: clamps
    REQUIRE(NextCodepointBoundary(text, 99) == 6); // past the end: clamps
}

TEST_CASE("NextCodepointBoundary tolerates malformed sequences without sticking", "[Utf8]") {
    using ned::text::NextCodepointBoundary;

    const std::string bad = "\xFF\x80\x80z";     // invalid lead + stray continuations
    REQUIRE(NextCodepointBoundary(bad, 0) == 3); // consumes the continuations, stops before 'z'
    REQUIRE(NextCodepointBoundary(bad, 3) == 4);
}

TEST_CASE("SnapDownToCodepointBoundary is a no-op at an already-aligned offset", "[Utf8]") {
    using ned::text::SnapDownToCodepointBoundary;

    const std::string text = "a\xC3\xA9\xE4\xB8\xADz"; // 'a' + 'é' + '中' + 'z'
    REQUIRE(SnapDownToCodepointBoundary(text, 0) == 0);
    REQUIRE(SnapDownToCodepointBoundary(text, 1) == 1); // start of 'é'
    REQUIRE(SnapDownToCodepointBoundary(text, 3) == 3); // start of '中'
    REQUIRE(SnapDownToCodepointBoundary(text, 6) == 6); // start of 'z'
    REQUIRE(SnapDownToCodepointBoundary(text, 7) == 7); // == text.size(), already a boundary
}

TEST_CASE("SnapDownToCodepointBoundary moves a mid-codepoint offset back to its start", "[Utf8]") {
    using ned::text::SnapDownToCodepointBoundary;

    const std::string text = "a\xC3\xA9\xE4\xB8\xADz"; // 'a' + 'é' (2 bytes) + '中' (3 bytes) + 'z'
    REQUIRE(SnapDownToCodepointBoundary(text, 2) == 1); // 2nd byte of 'é' -> 'é' start
    REQUIRE(SnapDownToCodepointBoundary(text, 4) == 3); // 2nd byte of '中' -> '中' start
    REQUIRE(SnapDownToCodepointBoundary(text, 5) == 3); // 3rd byte of '中' -> '中' start
}

TEST_CASE("SnapDownToCodepointBoundary bounds its walk against malformed input", "[Utf8]") {
    using ned::text::SnapDownToCodepointBoundary;

    // A run of stray continuation bytes with no lead byte at all -- must
    // still terminate (bounded to 3 steps) rather than walking to 0 or
    // looping forever.
    const std::string bad = std::string(10, '\x80');
    REQUIRE(SnapDownToCodepointBoundary(bad, 9) == 6);
}

TEST_CASE("SnapUpToCodepointBoundary is a no-op at an already-aligned offset", "[Utf8]") {
    using ned::text::SnapUpToCodepointBoundary;

    const std::string text = "a\xC3\xA9\xE4\xB8\xADz";
    REQUIRE(SnapUpToCodepointBoundary(text, 0) == 0);
    REQUIRE(SnapUpToCodepointBoundary(text, 1) == 1);
    REQUIRE(SnapUpToCodepointBoundary(text, 3) == 3);
    REQUIRE(SnapUpToCodepointBoundary(text, 6) == 6);
    REQUIRE(SnapUpToCodepointBoundary(text, 7) == 7);
}

TEST_CASE("SnapUpToCodepointBoundary moves a mid-codepoint offset forward past it", "[Utf8]") {
    using ned::text::SnapUpToCodepointBoundary;

    const std::string text = "a\xC3\xA9\xE4\xB8\xADz"; // 'a' + 'é' (2 bytes) + '中' (3 bytes) + 'z'
    REQUIRE(SnapUpToCodepointBoundary(text, 2) == 3);   // 2nd byte of 'é' -> start of '中'
    REQUIRE(SnapUpToCodepointBoundary(text, 4) == 6);   // 2nd byte of '中' -> start of 'z'
    REQUIRE(SnapUpToCodepointBoundary(text, 5) == 6);   // 3rd byte of '中' -> start of 'z'
}

TEST_CASE("SnapUpToCodepointBoundary bounds its walk against malformed input", "[Utf8]") {
    using ned::text::SnapUpToCodepointBoundary;

    const std::string bad = std::string(10, '\x80');
    REQUIRE(SnapUpToCodepointBoundary(bad, 0) == 3);
}
