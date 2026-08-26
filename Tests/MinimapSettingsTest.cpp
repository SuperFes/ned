#include <catch2/catch_test_macros.hpp>

#include "Editor/MinimapSettings.h"

using ned::editor::MinimapCharsPerDot;
using ned::editor::MinimapEnabled;
using ned::editor::MinimapWidth;
using ned::editor::SetMinimapCharsPerDot;
using ned::editor::SetMinimapEnabled;
using ned::editor::SetMinimapWidth;

namespace {

// Process-wide state (see MinimapSettings.h's own doc comment) -- mirrors
// TabWidthTest.cpp's own TabWidthGuard exactly, restoring defaults via RAII
// so a failed REQUIRE partway through a test can't leak state into the
// next one.
struct MinimapSettingsGuard {
    ~MinimapSettingsGuard() {
        SetMinimapEnabled(true);
        SetMinimapWidth(5);
        SetMinimapCharsPerDot(8);
    }
};

} // namespace

TEST_CASE("Minimap settings default to enabled, width 5, 8 chars per dot", "[MinimapSettings]") {
    const MinimapSettingsGuard guard;
    REQUIRE(MinimapEnabled());
    REQUIRE(MinimapWidth() == 5);
    REQUIRE(MinimapCharsPerDot() == 8);
}

TEST_CASE("SetMinimapEnabled/MinimapEnabled round-trip", "[MinimapSettings]") {
    const MinimapSettingsGuard guard;
    SetMinimapEnabled(false);
    REQUIRE_FALSE(MinimapEnabled());
    SetMinimapEnabled(true);
    REQUIRE(MinimapEnabled());
}

TEST_CASE("SetMinimapWidth/MinimapWidth round-trip and clamp", "[MinimapSettings]") {
    const MinimapSettingsGuard guard;
    SetMinimapWidth(8);
    REQUIRE(MinimapWidth() == 8);
    SetMinimapWidth(0);
    REQUIRE(MinimapWidth() == 1);
    SetMinimapWidth(-3);
    REQUIRE(MinimapWidth() == 1);
}

TEST_CASE("SetMinimapCharsPerDot/MinimapCharsPerDot round-trip and clamp", "[MinimapSettings]") {
    const MinimapSettingsGuard guard;
    SetMinimapCharsPerDot(4);
    REQUIRE(MinimapCharsPerDot() == 4);
    SetMinimapCharsPerDot(0);
    REQUIRE(MinimapCharsPerDot() == 1);
    SetMinimapCharsPerDot(-1);
    REQUIRE(MinimapCharsPerDot() == 1);
}

TEST_CASE("SetMinimapCharsPerDot accepts a fractional value", "[MinimapSettings]") {
    const MinimapSettingsGuard guard;
    SetMinimapCharsPerDot(8.5);
    REQUIRE(MinimapCharsPerDot() == 8.5);
    SetMinimapCharsPerDot(0.5); // below the 1.0 floor -- clamps up, same as any sub-1 value always has
    REQUIRE(MinimapCharsPerDot() == 1.0);
}
