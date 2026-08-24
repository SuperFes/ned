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
