#include <catch2/catch_test_macros.hpp>

#include "Editor/PageScroll.h"

using ned::editor::PageScrollFraction;
using ned::editor::SetPageScrollFraction;

namespace {

// Process-wide state (see PageScroll.h's own doc comment); every test that
// sets one must restore the default for the next test, mirroring
// TabWidthTest.cpp's own TabWidthGuard exactly.
struct PageScrollFractionGuard {
    ~PageScrollFractionGuard() {
        SetPageScrollFraction(0.65);
    }
};

} // namespace

TEST_CASE("PageScrollFraction defaults to 0.65", "[PageScroll]") {
    const PageScrollFractionGuard guard;
    REQUIRE(PageScrollFraction() == 0.65);
}

TEST_CASE("SetPageScrollFraction/PageScrollFraction round-trip", "[PageScroll]") {
    const PageScrollFractionGuard guard;
    SetPageScrollFraction(0.5);
    REQUIRE(PageScrollFraction() == 0.5);
    SetPageScrollFraction(0.9);
    REQUIRE(PageScrollFraction() == 0.9);
}

TEST_CASE("SetPageScrollFraction clamps to (0, 1]", "[PageScroll]") {
    const PageScrollFractionGuard guard;
    SetPageScrollFraction(0.0);
    REQUIRE(PageScrollFraction() == 0.01);
    SetPageScrollFraction(-5.0);
    REQUIRE(PageScrollFraction() == 0.01);
    SetPageScrollFraction(5.0);
    REQUIRE(PageScrollFraction() == 1.0);
}
