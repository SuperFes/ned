#include <catch2/catch_test_macros.hpp>

#include "Editor/RulerSettings.h"

using ned::editor::RulerColumn;
using ned::editor::RulerEnabled;
using ned::editor::SetRulerColumn;
using ned::editor::SetRulerEnabled;

namespace {

// Process-wide state (see RulerSettings.h's own doc comment) -- mirrors
// MinimapSettingsTest.cpp's own MinimapSettingsGuard exactly, restoring
// defaults via RAII so a failed REQUIRE partway through a test can't leak
// state into the next one.
struct RulerSettingsGuard {
    ~RulerSettingsGuard() {
        SetRulerEnabled(true);
        SetRulerColumn(80);
    }
};

} // namespace

TEST_CASE("Ruler settings default to enabled, column 80", "[RulerSettings]") {
    const RulerSettingsGuard guard;
    REQUIRE(RulerEnabled());
    REQUIRE(RulerColumn() == 80);
}

TEST_CASE("SetRulerEnabled/RulerEnabled round-trip", "[RulerSettings]") {
    const RulerSettingsGuard guard;
    SetRulerEnabled(false);
    REQUIRE_FALSE(RulerEnabled());
    SetRulerEnabled(true);
    REQUIRE(RulerEnabled());
}

TEST_CASE("SetRulerColumn/RulerColumn round-trip and clamp", "[RulerSettings]") {
    const RulerSettingsGuard guard;
    SetRulerColumn(100);
    REQUIRE(RulerColumn() == 100);
    SetRulerColumn(0);
    REQUIRE(RulerColumn() == 1);
    SetRulerColumn(-3);
    REQUIRE(RulerColumn() == 1);
}
