//
// Editor/Timestamp.h -- ISO 8601 parsing and compact ages.
//

#include <catch2/catch_test_macros.hpp>

#include "Editor/Timestamp.h"

using ned::editor::CompactAge;
using ned::editor::ParseIso8601;

TEST_CASE("ISO 8601 timestamps parse with and without offsets", "[Timestamp]") {
    // 2026-09-29T15:05:44Z
    constexpr std::int64_t kUtc = 1790694344;
    CHECK(ParseIso8601("2026-09-29T15:05:44Z") == kUtc);
    CHECK(ParseIso8601("2026-09-29T15:05:44") == kUtc);
    CHECK(ParseIso8601("2026-09-29T15:05:44.175Z") == kUtc);
    CHECK(ParseIso8601("2026-09-29T17:05:44.175+0200") == kUtc);
    CHECK(ParseIso8601("2026-09-29T17:05:44+02:00") == kUtc);
    CHECK(ParseIso8601("2026-09-29T08:05:44.000-0700") == kUtc);
}

TEST_CASE("Malformed timestamps don't parse", "[Timestamp]") {
    CHECK_FALSE(ParseIso8601(""));
    CHECK_FALSE(ParseIso8601("2026-09-29"));
    CHECK_FALSE(ParseIso8601("yesterday at noon!!"));
    CHECK_FALSE(ParseIso8601("2026-13-29T15:05:44Z"));
    CHECK_FALSE(ParseIso8601("2026-09-29T15:05:44Zjunk"));
    CHECK_FALSE(ParseIso8601("2026-09-29T15:05:44+02"));
}

TEST_CASE("Ages read in the largest whole unit", "[Timestamp]") {
    CHECK(CompactAge(-30) == "now");
    CHECK(CompactAge(59) == "now");
    CHECK(CompactAge(60) == "1m");
    CHECK(CompactAge(3599) == "59m");
    CHECK(CompactAge(3 * 3600) == "3h");
    CHECK(CompactAge(2 * 86400) == "2d");
    CHECK(CompactAge(9 * 86400) == "1w");
    CHECK(CompactAge(59 * 86400) == "8w");
    CHECK(CompactAge(60 * 86400) == "2mo");
    CHECK(CompactAge(400 * 86400) == "1y");
}
