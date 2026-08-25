#include <catch2/catch_test_macros.hpp>

#include "Editor/WhichKeySettings.h"

using ned::editor::SetWhichKeyEnabled;
using ned::editor::WhichKeyEnabled;

namespace {

// Process-wide state -- mirrors MinimapSettingsTest.cpp's own
// MinimapSettingsGuard exactly, restoring the default so a failed REQUIRE
// partway through a test can't leak state into the next one.
struct WhichKeySettingsGuard {
    ~WhichKeySettingsGuard() {
        SetWhichKeyEnabled(true);
    }
};

} // namespace

TEST_CASE("Which-key defaults to enabled", "[WhichKeySettings]") {
    const WhichKeySettingsGuard guard;
    REQUIRE(WhichKeyEnabled());
}

TEST_CASE("SetWhichKeyEnabled/WhichKeyEnabled round-trip", "[WhichKeySettings]") {
    const WhichKeySettingsGuard guard;
    SetWhichKeyEnabled(false);
    REQUIRE_FALSE(WhichKeyEnabled());
    SetWhichKeyEnabled(true);
    REQUIRE(WhichKeyEnabled());
}
