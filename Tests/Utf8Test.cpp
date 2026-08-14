#include <catch2/catch_test_macros.hpp>

#include "Text/Utf8.h"

using ned::text::EncodeCodepointUtf8;

TEST_CASE("EncodeCodepointUtf8 encodes ASCII as one byte", "[Utf8]") {
    REQUIRE(EncodeCodepointUtf8(U'a') == "a");
    REQUIRE(EncodeCodepointUtf8(U'\0') == std::string(1, '\0'));
}

TEST_CASE("EncodeCodepointUtf8 encodes multi-byte codepoints correctly", "[Utf8]") {
    REQUIRE(EncodeCodepointUtf8(0x00E9) == "\xC3\xA9");                 // 'é', 2 bytes
    REQUIRE(EncodeCodepointUtf8(0x4E2D) == "\xE4\xB8\xAD");             // '中', 3 bytes
    REQUIRE(EncodeCodepointUtf8(0x1F600) == "\xF0\x9F\x98\x80");        // grinning face emoji, 4 bytes
}
