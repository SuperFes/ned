#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "Editor/Sparkline.h"

using ned::editor::BuildBlockSparkline;
using ned::editor::TryParseNumeric;

TEST_CASE("TryParseNumeric accepts trimmed decimal/scientific numbers, rejects everything else", "[Sparkline]") {
    double value = 0.0;

    REQUIRE(TryParseNumeric("42", value));
    REQUIRE(value == 42.0);

    REQUIRE(TryParseNumeric("  -3.5  ", value));
    REQUIRE(value == -3.5);

    REQUIRE(TryParseNumeric("1e6", value));
    REQUIRE(value == 1e6);

    REQUIRE_FALSE(TryParseNumeric("", value));
    REQUIRE_FALSE(TryParseNumeric("   ", value));
    REQUIRE_FALSE(TryParseNumeric("42abc", value));
    REQUIRE_FALSE(TryParseNumeric("1, 2", value));
    REQUIRE_FALSE(TryParseNumeric("true", value));
    REQUIRE_FALSE(TryParseNumeric("<optimized out>", value));
    REQUIRE_FALSE(TryParseNumeric("0x2a", value)); // hex deliberately not recognized
}

TEST_CASE("BuildBlockSparkline returns empty for empty input or a zero width", "[Sparkline]") {
    REQUIRE(BuildBlockSparkline({}).empty());
    const std::vector<double> values = {1.0, 2.0, 3.0};
    REQUIRE(BuildBlockSparkline(values, 0).empty());
}

TEST_CASE("BuildBlockSparkline emits one glyph per value when under maxWidth", "[Sparkline]") {
    const std::vector<double> values = {0.0, 10.0};
    const std::string         result = BuildBlockSparkline(values, 40);
    // Two 3-byte UTF-8 glyphs.
    REQUIRE(result.size() == 6);
    // Lowest value -> lowest glyph, highest value -> the full block.
    REQUIRE(result.substr(0, 3) == "\xE2\x96\x81"); // U+2581 LOWER ONE EIGHTH BLOCK
    REQUIRE(result.substr(3, 3) == "\xE2\x96\x88"); // U+2588 FULL BLOCK
}

TEST_CASE("BuildBlockSparkline renders a flat/constant series as a steady mid-height line", "[Sparkline]") {
    const std::vector<double> values(5, 7.0);
    const std::string         result = BuildBlockSparkline(values, 40);
    REQUIRE(result.size() == 15); // 5 glyphs, 3 bytes each
    for (std::size_t i = 0; i < 5; ++i) {
        REQUIRE(result.substr(i * 3, 3) == "\xE2\x96\x84"); // U+2584, level 3 of 0..7
    }
}

TEST_CASE("BuildBlockSparkline downsamples by bucket-averaging when values exceed maxWidth", "[Sparkline]") {
    std::vector<double> values;
    for (int i = 0; i < 100; ++i) {
        values.push_back(static_cast<double>(i));
    }
    const std::string result = BuildBlockSparkline(values, 10);
    REQUIRE(result.size() == 30); // exactly maxWidth glyphs, 3 bytes each -- never longer than requested
}

TEST_CASE("BuildBlockSparkline is monotonic for a monotonic input", "[Sparkline]") {
    const std::vector<double> values = {1.0, 2.0, 4.0, 8.0, 16.0};
    const std::string         result = BuildBlockSparkline(values, 40);
    REQUIRE(result.size() == 15);
    // Each successive glyph's codepoint must be >= the previous one -- an
    // ascending series never dips.
    for (std::size_t i = 1; i < 5; ++i) {
        REQUIRE(result.substr(i * 3, 3) >= result.substr((i - 1) * 3, 3));
    }
}
