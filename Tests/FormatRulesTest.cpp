#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <string>

#include "Editor/FormatRules.h"

using ned::editor::BracePlacement;
using ned::editor::BracePlacementByName;
using ned::editor::BracePlacementName;
using ned::editor::BreakRuleFor;
using ned::editor::BreakRuleValue;
using ned::editor::FormatRuleGeneration;
using ned::editor::SetBraceCollapseEmpty;
using ned::editor::SetBraceCollapseSimple;
using ned::editor::SetBracePlacement;
using ned::editor::SetBreakAfter;
using ned::editor::SetBreakBefore;
using ned::editor::SetSpaceAfter;
using ned::editor::SetSpaceBefore;
using ned::editor::SetSpaceWithin;
using ned::editor::SpaceRuleFor;
using ned::editor::SpaceRuleValue;

namespace {

// Every override set by these tests is cleared afterward -- process-wide
// state, same "guaranteed reset" precedent SyntaxThemeTest.cpp's
// SyntaxThemeGuard establishes.
struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetSpaceBefore("format-rules-test.capture", std::nullopt);
        SetSpaceAfter("format-rules-test.capture", std::nullopt);
        SetSpaceWithin("format-rules-test.capture", std::nullopt);
        SetSpaceBefore("cpp/format-rules-test.capture", std::nullopt);
        SetBreakBefore("format-rules-test.capture", std::nullopt);
        SetBreakAfter("format-rules-test.capture", std::nullopt);
        SetBracePlacement("format-rules-test.capture", std::nullopt);
        SetBraceCollapseEmpty("format-rules-test.capture", std::nullopt);
        SetBraceCollapseSimple("format-rules-test.capture", std::nullopt);
        SetBracePlacement("cpp/format-rules-test.capture", std::nullopt);
    }
};

} // namespace

TEST_CASE("A capture with no override has every space/break field unset", "[FormatRules]") {
    REQUIRE_FALSE(SpaceRuleFor("format-rules-test.unconfigured").before.has_value());
    REQUIRE_FALSE(SpaceRuleFor("format-rules-test.unconfigured").after.has_value());
    REQUIRE_FALSE(SpaceRuleFor("format-rules-test.unconfigured").within.has_value());
    REQUIRE_FALSE(BreakRuleFor("format-rules-test.unconfigured").before.has_value());
    REQUIRE_FALSE(BreakRuleFor("format-rules-test.unconfigured").placement.has_value());
}

TEST_CASE("Setting and clearing a space rule round-trips and bumps the generation", "[FormatRules]") {
    FormatRulesGuard guard;

    const std::size_t before = FormatRuleGeneration();
    SetSpaceBefore("format-rules-test.capture", true);
    REQUIRE(SpaceRuleFor("format-rules-test.capture").before == true);
    REQUIRE(FormatRuleGeneration() > before);

    SetSpaceBefore("format-rules-test.capture", std::nullopt);
    REQUIRE_FALSE(SpaceRuleFor("format-rules-test.capture").before.has_value());
}

TEST_CASE("Space rule fields are independent", "[FormatRules]") {
    FormatRulesGuard guard;

    SetSpaceBefore("format-rules-test.capture", true);
    SetSpaceAfter("format-rules-test.capture", false);
    SetSpaceWithin("format-rules-test.capture", true);

    const SpaceRuleValue value = SpaceRuleFor("format-rules-test.capture");
    REQUIRE(value.before == true);
    REQUIRE(value.after == false);
    REQUIRE(value.within == true);
}

TEST_CASE("SpaceRuleFor(name, language) tries the language-scoped key first", "[FormatRules]") {
    FormatRulesGuard guard;

    SetSpaceBefore("format-rules-test.capture", true);            // shared rule
    SetSpaceBefore("cpp/format-rules-test.capture", false);       // cpp's own override

    REQUIRE(SpaceRuleFor("format-rules-test.capture", "cpp").before == false);
    REQUIRE(SpaceRuleFor("format-rules-test.capture", "python").before == true); // falls through to the shared rule
    REQUIRE(SpaceRuleFor("format-rules-test.capture", "").before == true);       // empty language == unscoped
    REQUIRE(SpaceRuleFor("format-rules-test.capture").before == true);           // unscoped overload, unaffected
}

TEST_CASE("BreakRuleFor(name, language) resolves the same way, per-field", "[FormatRules]") {
    FormatRulesGuard guard;

    SetBreakBefore("format-rules-test.capture", true);
    SetBracePlacement("cpp/format-rules-test.capture", BracePlacement::NextLine);

    const BreakRuleValue scoped = BreakRuleFor("format-rules-test.capture", "cpp");
    // The scoped key only sets :placement -- a real per-node result would
    // merge both languages' worth of fields once a pass exists, but the
    // resolution primitive itself is whole-entry, matching
    // SyntaxClassOverrideForCapture's own (non-merging) two-argument shape.
    REQUIRE(scoped.placement == BracePlacement::NextLine);
    REQUIRE_FALSE(scoped.before.has_value()); // cpp's entry never set :before

    REQUIRE(BreakRuleFor("format-rules-test.capture", "python").before == true); // falls through, cpp's entry unset for python
}

TEST_CASE("BracePlacementByName/BracePlacementName round-trip for every value", "[FormatRules]") {
    for (const BracePlacement placement :
        {BracePlacement::SameLine, BracePlacement::NextLine, BracePlacement::NextLineIndented}) {
        REQUIRE(BracePlacementByName(BracePlacementName(placement)) == placement);
    }
}

TEST_CASE("BracePlacementByName throws for an unrecognized name", "[FormatRules]") {
    REQUIRE_THROWS_AS(BracePlacementByName("not-a-real-placement"), std::runtime_error);
}

TEST_CASE("An invalid capture name throws for both rule kinds", "[FormatRules]") {
    REQUIRE_THROWS_AS(SetSpaceBefore("", true), std::runtime_error);
    REQUIRE_THROWS_AS(SetSpaceBefore("@leading-at", true), std::runtime_error);
    REQUIRE_THROWS_AS(SetSpaceBefore(".leading-dot", true), std::runtime_error);
    REQUIRE_THROWS_AS(SetSpaceBefore("trailing-dot.", true), std::runtime_error);
    REQUIRE_THROWS_AS(SetSpaceBefore("double..dot", true), std::runtime_error);
    REQUIRE_THROWS_AS(SetSpaceBefore("has space", true), std::runtime_error);
    REQUIRE_THROWS_AS(SetBreakBefore("", true), std::runtime_error);
}
