#include <catch2/catch_test_macros.hpp>

#include "Editor/WhitespaceSettings.h"

using ned::editor::IndentGuidesEnabled;
using ned::editor::SetIndentGuidesEnabled;
using ned::editor::SetTrailingWhitespaceHighlightEnabled;
using ned::editor::TrailingWhitespaceHighlightEnabled;

namespace {

// Process-wide state (see WhitespaceSettings.h's own doc comment) -- mirrors
// MinimapSettingsTest.cpp's own guard exactly, restoring defaults via RAII
// so a failed REQUIRE partway through a test can't leak state into the next
// one.
struct WhitespaceSettingsGuard {
    ~WhitespaceSettingsGuard() {
        SetTrailingWhitespaceHighlightEnabled(false);
        SetIndentGuidesEnabled(false);
    }
};

} // namespace

TEST_CASE("Whitespace settings default to disabled", "[WhitespaceSettings]") {
    const WhitespaceSettingsGuard guard;
    REQUIRE_FALSE(TrailingWhitespaceHighlightEnabled());
    REQUIRE_FALSE(IndentGuidesEnabled());
}

TEST_CASE("SetTrailingWhitespaceHighlightEnabled/TrailingWhitespaceHighlightEnabled round-trip", "[WhitespaceSettings]") {
    const WhitespaceSettingsGuard guard;
    SetTrailingWhitespaceHighlightEnabled(true);
    REQUIRE(TrailingWhitespaceHighlightEnabled());
    SetTrailingWhitespaceHighlightEnabled(false);
    REQUIRE_FALSE(TrailingWhitespaceHighlightEnabled());
}

TEST_CASE("SetIndentGuidesEnabled/IndentGuidesEnabled round-trip", "[WhitespaceSettings]") {
    const WhitespaceSettingsGuard guard;
    SetIndentGuidesEnabled(true);
    REQUIRE(IndentGuidesEnabled());
    SetIndentGuidesEnabled(false);
    REQUIRE_FALSE(IndentGuidesEnabled());
}
