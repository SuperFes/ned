#include <catch2/catch_test_macros.hpp>

#include "Editor/TabWidth.h"

using ned::editor::SetTabWidth;
using ned::editor::TabWidth;

namespace {

// TabWidth is process-wide state (see TabWidth.h's own doc comment); every
// test that sets one must restore the default for the next test, guaranteed
// via RAII rather than a manual reset at the end (which a failed REQUIRE
// partway through would skip). Mirrors FormatOnSaveTest.cpp's own
// FormatCommandGuard exactly.
struct TabWidthGuard {
    ~TabWidthGuard() {
        SetTabWidth(4);
    }
};

} // namespace

TEST_CASE("TabWidth defaults to 4", "[TabWidth]") {
    const TabWidthGuard guard;
    REQUIRE(TabWidth() == 4);
}

TEST_CASE("SetTabWidth/TabWidth round-trip", "[TabWidth]") {
    const TabWidthGuard guard;
    SetTabWidth(8);
    REQUIRE(TabWidth() == 8);
    SetTabWidth(2);
    REQUIRE(TabWidth() == 2);
}

TEST_CASE("SetTabWidth clamps a non-positive width to 1", "[TabWidth]") {
    const TabWidthGuard guard;
    SetTabWidth(0);
    REQUIRE(TabWidth() == 1);
    SetTabWidth(-5);
    REQUIRE(TabWidth() == 1);
}
