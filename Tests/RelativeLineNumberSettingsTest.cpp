#include <catch2/catch_test_macros.hpp>

#include "Editor/RelativeLineNumberSettings.h"

using ned::editor::RelativeLineNumbersEnabled;
using ned::editor::SetRelativeLineNumbersEnabled;

namespace {

// Process-wide state -- mirrors MinimapSettingsTest.cpp's own
// MinimapSettingsGuard exactly, restoring the default so a failed REQUIRE
// partway through a test can't leak state into the next one.
struct RelativeLineNumberSettingsGuard {
    ~RelativeLineNumberSettingsGuard() {
        SetRelativeLineNumbersEnabled(false);
    }
};

} // namespace

TEST_CASE("Relative line numbers default to disabled", "[RelativeLineNumberSettings]") {
    const RelativeLineNumberSettingsGuard guard;
    REQUIRE_FALSE(RelativeLineNumbersEnabled());
}

TEST_CASE("SetRelativeLineNumbersEnabled/RelativeLineNumbersEnabled round-trip", "[RelativeLineNumberSettings]") {
    const RelativeLineNumberSettingsGuard guard;
    SetRelativeLineNumbersEnabled(true);
    REQUIRE(RelativeLineNumbersEnabled());
    SetRelativeLineNumbersEnabled(false);
    REQUIRE_FALSE(RelativeLineNumbersEnabled());
}
