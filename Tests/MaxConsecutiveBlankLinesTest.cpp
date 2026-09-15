#include <catch2/catch_test_macros.hpp>

#include "Editor/MaxConsecutiveBlankLines.h"

using ned::editor::MaxConsecutiveBlankLines;
using ned::editor::SetMaxConsecutiveBlankLines;

namespace {

// Process-wide state (see MaxConsecutiveBlankLines.h's own doc comment);
// every test that sets it must restore the default for the next test,
// mirroring TrimOnSaveTest.cpp's own guard exactly.
struct MaxConsecutiveBlankLinesGuard {
    ~MaxConsecutiveBlankLinesGuard() {
        SetMaxConsecutiveBlankLines(2);
    }
};

} // namespace

TEST_CASE("MaxConsecutiveBlankLines defaults to 2", "[MaxConsecutiveBlankLines]") {
    const MaxConsecutiveBlankLinesGuard guard;
    REQUIRE(MaxConsecutiveBlankLines() == 2);
}

TEST_CASE("SetMaxConsecutiveBlankLines/MaxConsecutiveBlankLines round-trip", "[MaxConsecutiveBlankLines]") {
    const MaxConsecutiveBlankLinesGuard guard;
    SetMaxConsecutiveBlankLines(0);
    REQUIRE(MaxConsecutiveBlankLines() == 0);
    SetMaxConsecutiveBlankLines(5);
    REQUIRE(MaxConsecutiveBlankLines() == 5);
}

TEST_CASE("SetMaxConsecutiveBlankLines(nullopt) disables the rule", "[MaxConsecutiveBlankLines]") {
    const MaxConsecutiveBlankLinesGuard guard;
    SetMaxConsecutiveBlankLines(std::nullopt);
    REQUIRE_FALSE(MaxConsecutiveBlankLines().has_value());
}

TEST_CASE("SetMaxConsecutiveBlankLines with a negative value is the same as nullopt", "[MaxConsecutiveBlankLines]") {
    const MaxConsecutiveBlankLinesGuard guard;
    SetMaxConsecutiveBlankLines(-1);
    REQUIRE_FALSE(MaxConsecutiveBlankLines().has_value());
}
