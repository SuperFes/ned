#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Text/Grapheme.h"
#include "Text/Rope.h"

using ned::text::NextGraphemeBoundary;
using ned::text::PreviousGraphemeBoundary;
using ned::text::Rope;
using ned::text::SnapToGraphemeBoundary;

TEST_CASE("Grapheme boundaries for plain ASCII are per-codepoint", "[Grapheme]") {
    const Rope rope("abc");

    REQUIRE(NextGraphemeBoundary(rope, 0) == 1);
    REQUIRE(NextGraphemeBoundary(rope, 1) == 2);
    REQUIRE(NextGraphemeBoundary(rope, 2) == 3);
    REQUIRE(NextGraphemeBoundary(rope, 3) == 3); // at end, clamps

    REQUIRE(PreviousGraphemeBoundary(rope, 3) == 2);
    REQUIRE(PreviousGraphemeBoundary(rope, 2) == 1);
    REQUIRE(PreviousGraphemeBoundary(rope, 1) == 0);
    REQUIRE(PreviousGraphemeBoundary(rope, 0) == 0);
}

TEST_CASE("Combining accent forms a single grapheme cluster with its base character", "[Grapheme]") {
    // "e" (1 byte) + U+0301 COMBINING ACUTE ACCENT (2 bytes) = "é" as two codepoints, one cluster.
    const std::string text = "xe\xCC\x81y"; // x [e + combining acute] y
    const Rope         rope(text);

    REQUIRE(rope.ByteLength() == 5); // 'x' + 'e' + 0xCC 0x81 + 'y'

    // Boundary before 'x' is at 0, after 'x' (start of the combined cluster) at 1.
    REQUIRE(NextGraphemeBoundary(rope, 0) == 1);
    // From the start of the cluster, the whole "e + combining accent" is skipped in one step.
    REQUIRE(NextGraphemeBoundary(rope, 1) == 4);
    REQUIRE(NextGraphemeBoundary(rope, 4) == 5);

    REQUIRE(PreviousGraphemeBoundary(rope, 5) == 4);
    REQUIRE(PreviousGraphemeBoundary(rope, 4) == 1);
    REQUIRE(PreviousGraphemeBoundary(rope, 1) == 0);
}

TEST_CASE("Regional indicator pair (flag emoji) forms a single grapheme cluster", "[Grapheme]") {
    // US flag: REGIONAL INDICATOR SYMBOL LETTER U (U+1F1FA) + LETTER S (U+1F1F8),
    // 4 bytes each, 8 bytes total, one grapheme cluster per UAX #29 GB12/GB13.
    const std::string flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
    const std::string text = "a" + flag + "b";
    const Rope         rope(text);

    REQUIRE(rope.ByteLength() == 1 + 8 + 1);

    REQUIRE(NextGraphemeBoundary(rope, 0) == 1);   // past 'a'
    REQUIRE(NextGraphemeBoundary(rope, 1) == 9);   // whole flag in one step
    REQUIRE(NextGraphemeBoundary(rope, 9) == 10);  // past 'b'

    REQUIRE(PreviousGraphemeBoundary(rope, 10) == 9);
    REQUIRE(PreviousGraphemeBoundary(rope, 9) == 1);
    REQUIRE(PreviousGraphemeBoundary(rope, 1) == 0);
}

TEST_CASE("SnapToGraphemeBoundary lands on offset itself when it's already a boundary", "[Grapheme]") {
    const Rope rope("abc");

    REQUIRE(SnapToGraphemeBoundary(rope, 0) == 0);
    REQUIRE(SnapToGraphemeBoundary(rope, 1) == 1);
    REQUIRE(SnapToGraphemeBoundary(rope, 3) == 3);
    REQUIRE(SnapToGraphemeBoundary(rope, 999) == 3); // clamps past end
}

TEST_CASE("SnapToGraphemeBoundary pulls a mid-cluster offset back to the cluster start", "[Grapheme]") {
    const std::string text = "xe\xCC\x81y"; // x [e + combining acute] y
    const Rope         rope(text);

    // Byte 2 is the first byte of the combining accent, inside the "e + accent"
    // cluster that starts at byte 1. Byte 3 is mid-codepoint (a continuation
    // byte of the accent) -- also inside the same cluster.
    REQUIRE(SnapToGraphemeBoundary(rope, 2) == 1);
    REQUIRE(SnapToGraphemeBoundary(rope, 3) == 1);
    REQUIRE(SnapToGraphemeBoundary(rope, 1) == 1); // already a boundary
    REQUIRE(SnapToGraphemeBoundary(rope, 4) == 4); // already a boundary
}
