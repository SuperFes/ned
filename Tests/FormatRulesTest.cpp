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
using ned::editor::SetWrapForceTrailingComma;
using ned::editor::SetWrapPolicy;
using ned::editor::SpaceRuleFor;
using ned::editor::SpaceRuleValue;
using ned::editor::WrapPolicy;
using ned::editor::WrapPolicyByName;
using ned::editor::WrapPolicyName;
using ned::editor::WrapRuleFor;
using ned::editor::WrapRuleValue;

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
        SetWrapPolicy("format-rules-test.capture", std::nullopt);
        SetWrapForceTrailingComma("format-rules-test.capture", std::nullopt);
        SetWrapPolicy("cpp/format-rules-test.capture", std::nullopt);
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

    SetSpaceBefore("format-rules-test.capture", true);      // shared rule
    SetSpaceBefore("cpp/format-rules-test.capture", false); // cpp's own override

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

TEST_CASE("A capture with no wrap override has every field unset", "[FormatRules]") {
    REQUIRE_FALSE(WrapRuleFor("format-rules-test.unconfigured").policy.has_value());
    REQUIRE_FALSE(WrapRuleFor("format-rules-test.unconfigured").forceTrailingComma.has_value());
}

TEST_CASE("Setting and clearing a wrap rule round-trips and bumps the generation", "[FormatRules]") {
    FormatRulesGuard guard;

    const std::size_t before = FormatRuleGeneration();
    SetWrapPolicy("format-rules-test.capture", WrapPolicy::Always);
    REQUIRE(WrapRuleFor("format-rules-test.capture").policy == WrapPolicy::Always);
    REQUIRE(FormatRuleGeneration() > before);

    SetWrapPolicy("format-rules-test.capture", std::nullopt);
    REQUIRE_FALSE(WrapRuleFor("format-rules-test.capture").policy.has_value());
}

TEST_CASE("Wrap rule fields are independent", "[FormatRules]") {
    FormatRulesGuard guard;

    SetWrapPolicy("format-rules-test.capture", WrapPolicy::Never);
    SetWrapForceTrailingComma("format-rules-test.capture", true);

    const WrapRuleValue value = WrapRuleFor("format-rules-test.capture");
    REQUIRE(value.policy == WrapPolicy::Never);
    REQUIRE(value.forceTrailingComma == true);
}

TEST_CASE("WrapRuleFor(name, language) tries the language-scoped key first", "[FormatRules]") {
    FormatRulesGuard guard;

    SetWrapPolicy("format-rules-test.capture", WrapPolicy::Always);    // shared rule
    SetWrapPolicy("cpp/format-rules-test.capture", WrapPolicy::Never); // cpp's own override

    REQUIRE(WrapRuleFor("format-rules-test.capture", "cpp").policy == WrapPolicy::Never);
    REQUIRE(WrapRuleFor("format-rules-test.capture", "python").policy == WrapPolicy::Always); // falls through
    REQUIRE(WrapRuleFor("format-rules-test.capture", "").policy == WrapPolicy::Always);       // empty == unscoped
    REQUIRE(WrapRuleFor("format-rules-test.capture").policy == WrapPolicy::Always);           // unscoped, unaffected
}

TEST_CASE("WrapPolicyByName/WrapPolicyName round-trip for every value", "[FormatRules]") {
    for (const WrapPolicy policy : {WrapPolicy::Never, WrapPolicy::Always}) {
        REQUIRE(WrapPolicyByName(WrapPolicyName(policy)) == policy);
    }
}

TEST_CASE("WrapPolicyByName throws for an unrecognized name", "[FormatRules]") {
    REQUIRE_THROWS_AS(WrapPolicyByName("not-a-real-policy"), std::runtime_error);
}

TEST_CASE("A capture with no case override has every field unset", "[FormatRules]") {
    REQUIRE_FALSE(ned::editor::CaseRuleFor("format-rules-test.unconfigured").convention.has_value());
}

TEST_CASE("Setting and clearing a case rule round-trips and bumps the generation", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::CaseRuleFor;
    using ned::editor::SetCaseConvention;

    const std::size_t before = FormatRuleGeneration();
    SetCaseConvention("format-rules-test.capture", CaseConvention::SnakeCase);
    REQUIRE(CaseRuleFor("format-rules-test.capture").convention == CaseConvention::SnakeCase);
    REQUIRE(FormatRuleGeneration() > before);

    SetCaseConvention("format-rules-test.capture", std::nullopt);
    REQUIRE_FALSE(CaseRuleFor("format-rules-test.capture").convention.has_value());
}

TEST_CASE("CaseConventionByName/CaseConventionName round-trip for every value", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::CaseConventionByName;
    using ned::editor::CaseConventionName;
    for (const CaseConvention convention :
         {CaseConvention::None, CaseConvention::Lowercase, CaseConvention::Uppercase, CaseConvention::CamelCase,
          CaseConvention::PascalCase, CaseConvention::SnakeCase, CaseConvention::LeadingSnakeCase,
          CaseConvention::UpperSnakeCase, CaseConvention::ScreamingSnakeCase, CaseConvention::LispCase}) {
        REQUIRE(CaseConventionByName(CaseConventionName(convention)) == convention);
    }
}

TEST_CASE("CaseConventionByName throws for an unrecognized name", "[FormatRules]") {
    REQUIRE_THROWS_AS(ned::editor::CaseConventionByName("not-a-real-convention"), std::runtime_error);
}

TEST_CASE("MatchesCaseConvention: None matches anything, including the empty string", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("", CaseConvention::None));
    REQUIRE(MatchesCaseConvention("Anything_At-ALL123", CaseConvention::None));
}

TEST_CASE("MatchesCaseConvention: every other convention rejects the empty string", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    for (const CaseConvention convention :
         {CaseConvention::Lowercase, CaseConvention::Uppercase, CaseConvention::CamelCase, CaseConvention::PascalCase,
          CaseConvention::SnakeCase, CaseConvention::LeadingSnakeCase, CaseConvention::UpperSnakeCase,
          CaseConvention::ScreamingSnakeCase, CaseConvention::LispCase}) {
        REQUIRE_FALSE(MatchesCaseConvention("", convention));
    }
}

TEST_CASE("MatchesCaseConvention: lowercase", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("foo", CaseConvention::Lowercase));
    REQUIRE(MatchesCaseConvention("foo123", CaseConvention::Lowercase));
    REQUIRE_FALSE(MatchesCaseConvention("Foo", CaseConvention::Lowercase));
    REQUIRE_FALSE(MatchesCaseConvention("foo_bar", CaseConvention::Lowercase));
    REQUIRE_FALSE(MatchesCaseConvention("123foo", CaseConvention::Lowercase));
}

TEST_CASE("MatchesCaseConvention: uppercase", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("FOO", CaseConvention::Uppercase));
    REQUIRE(MatchesCaseConvention("FOO123", CaseConvention::Uppercase));
    REQUIRE_FALSE(MatchesCaseConvention("foo", CaseConvention::Uppercase));
    REQUIRE_FALSE(MatchesCaseConvention("FOO_BAR", CaseConvention::Uppercase));
}

TEST_CASE("MatchesCaseConvention: camelCase", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("fooBar", CaseConvention::CamelCase));
    REQUIRE(MatchesCaseConvention("foo", CaseConvention::CamelCase)); // a bare single word is trivially valid
    REQUIRE(MatchesCaseConvention("fooBar123", CaseConvention::CamelCase));
    REQUIRE_FALSE(MatchesCaseConvention("FooBar", CaseConvention::CamelCase));
    REQUIRE_FALSE(MatchesCaseConvention("foo_bar", CaseConvention::CamelCase));
}

TEST_CASE("MatchesCaseConvention: PascalCase", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("FooBar", CaseConvention::PascalCase));
    REQUIRE(MatchesCaseConvention("Foo", CaseConvention::PascalCase));
    REQUIRE_FALSE(MatchesCaseConvention("fooBar", CaseConvention::PascalCase));
    REQUIRE_FALSE(MatchesCaseConvention("Foo_Bar", CaseConvention::PascalCase));
}

TEST_CASE("MatchesCaseConvention: snake_case", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("foo_bar", CaseConvention::SnakeCase));
    REQUIRE(MatchesCaseConvention("foo", CaseConvention::SnakeCase));
    REQUIRE(MatchesCaseConvention("foo_bar_baz123", CaseConvention::SnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("Foo_bar", CaseConvention::SnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("foo_Bar", CaseConvention::SnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("FOO_BAR", CaseConvention::SnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("_foo", CaseConvention::SnakeCase));     // leading separator
    REQUIRE_FALSE(MatchesCaseConvention("foo_", CaseConvention::SnakeCase));     // trailing separator
    REQUIRE_FALSE(MatchesCaseConvention("foo__bar", CaseConvention::SnakeCase)); // doubled separator
}

TEST_CASE("MatchesCaseConvention: Leading_snake_case", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("Foo_bar", CaseConvention::LeadingSnakeCase));
    REQUIRE(MatchesCaseConvention("Foo", CaseConvention::LeadingSnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("foo_bar", CaseConvention::LeadingSnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("Foo_Bar", CaseConvention::LeadingSnakeCase));
}

TEST_CASE("MatchesCaseConvention: Upper_Snake_Case", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("Foo_Bar", CaseConvention::UpperSnakeCase));
    REQUIRE(MatchesCaseConvention("Foo", CaseConvention::UpperSnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("Foo_bar", CaseConvention::UpperSnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("foo_Bar", CaseConvention::UpperSnakeCase));
}

TEST_CASE("MatchesCaseConvention: SCREAMING_SNAKE_CASE", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("FOO_BAR", CaseConvention::ScreamingSnakeCase));
    REQUIRE(MatchesCaseConvention("FOO", CaseConvention::ScreamingSnakeCase));
    REQUIRE(MatchesCaseConvention("MAX_VALUE_123", CaseConvention::ScreamingSnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("foo_bar", CaseConvention::ScreamingSnakeCase));
    REQUIRE_FALSE(MatchesCaseConvention("Foo_Bar", CaseConvention::ScreamingSnakeCase));
}

TEST_CASE("MatchesCaseConvention: lisp-case", "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::MatchesCaseConvention;
    REQUIRE(MatchesCaseConvention("foo-bar", CaseConvention::LispCase));
    REQUIRE(MatchesCaseConvention("foo", CaseConvention::LispCase));
    REQUIRE_FALSE(MatchesCaseConvention("Foo-bar", CaseConvention::LispCase));
    REQUIRE_FALSE(MatchesCaseConvention("FOO-BAR", CaseConvention::LispCase));
    REQUIRE_FALSE(MatchesCaseConvention("foo_bar", CaseConvention::LispCase)); // wrong separator
}

TEST_CASE("CaseRuleFor(name, language) resolves the language-scoped key first, matching "
          "every other rule kind's own precedent",
          "[FormatRules]") {
    using ned::editor::CaseConvention;
    using ned::editor::CaseRuleFor;
    using ned::editor::SetCaseConvention;
    struct Guard {
        ~Guard() {
            SetCaseConvention("format-rules-test.capture", std::nullopt);
            SetCaseConvention("cpp/format-rules-test.capture", std::nullopt);
        }
    } guard;

    SetCaseConvention("format-rules-test.capture", CaseConvention::CamelCase);     // shared rule
    SetCaseConvention("cpp/format-rules-test.capture", CaseConvention::SnakeCase); // cpp's own override

    REQUIRE(CaseRuleFor("format-rules-test.capture", "cpp").convention == CaseConvention::SnakeCase);
    REQUIRE(CaseRuleFor("format-rules-test.capture", "python").convention == CaseConvention::CamelCase);
}

TEST_CASE("A capture with no align override has every field unset", "[FormatRules]") {
    REQUIRE_FALSE(ned::editor::AlignRuleFor("format-rules-test.unconfigured").enabled.has_value());
}

TEST_CASE("Setting and clearing an align rule round-trips and bumps the generation", "[FormatRules]") {
    using ned::editor::AlignRuleFor;
    using ned::editor::SetAlignEnabled;
    struct Guard {
        ~Guard() {
            SetAlignEnabled("format-rules-test.capture", std::nullopt);
        }
    } guard;

    const std::size_t before = FormatRuleGeneration();
    SetAlignEnabled("format-rules-test.capture", true);
    REQUIRE(AlignRuleFor("format-rules-test.capture").enabled == true);
    REQUIRE(FormatRuleGeneration() > before);

    SetAlignEnabled("format-rules-test.capture", std::nullopt);
    REQUIRE_FALSE(AlignRuleFor("format-rules-test.capture").enabled.has_value());
}

TEST_CASE("AlignRuleFor(name, language) resolves the language-scoped key first", "[FormatRules]") {
    using ned::editor::AlignRuleFor;
    using ned::editor::SetAlignEnabled;
    struct Guard {
        ~Guard() {
            SetAlignEnabled("format-rules-test.capture", std::nullopt);
            SetAlignEnabled("cpp/format-rules-test.capture", std::nullopt);
        }
    } guard;

    SetAlignEnabled("format-rules-test.capture", true);
    SetAlignEnabled("cpp/format-rules-test.capture", false);

    REQUIRE(AlignRuleFor("format-rules-test.capture", "cpp").enabled == false);
    REQUIRE(AlignRuleFor("format-rules-test.capture", "python").enabled == true);
}

TEST_CASE("A capture with no arrange override has every field unset", "[FormatRules]") {
    REQUIRE_FALSE(ned::editor::ArrangeRuleFor("format-rules-test.unconfigured").enabled.has_value());
    REQUIRE_FALSE(ned::editor::ArrangeRuleFor("format-rules-test.unconfigured").caseInsensitive.has_value());
}

TEST_CASE("Arrange rule fields are independent", "[FormatRules]") {
    using ned::editor::ArrangeRuleFor;
    using ned::editor::SetArrangeCaseInsensitive;
    using ned::editor::SetArrangeEnabled;
    struct Guard {
        ~Guard() {
            SetArrangeEnabled("format-rules-test.capture", std::nullopt);
            SetArrangeCaseInsensitive("format-rules-test.capture", std::nullopt);
        }
    } guard;

    SetArrangeEnabled("format-rules-test.capture", true);
    REQUIRE(ArrangeRuleFor("format-rules-test.capture").enabled == true);
    REQUIRE_FALSE(ArrangeRuleFor("format-rules-test.capture").caseInsensitive.has_value());

    SetArrangeCaseInsensitive("format-rules-test.capture", true);
    REQUIRE(ArrangeRuleFor("format-rules-test.capture").caseInsensitive == true);
    REQUIRE(ArrangeRuleFor("format-rules-test.capture").enabled == true); // unaffected
}

TEST_CASE("A capture with no rewrite override has every field unset", "[FormatRules]") {
    REQUIRE_FALSE(ned::editor::RewriteRuleFor("format-rules-test.unconfigured").quoteStyle.has_value());
}

TEST_CASE("QuoteStyleByName/QuoteStyleName round-trip for every value", "[FormatRules]") {
    using ned::editor::QuoteStyle;
    using ned::editor::QuoteStyleByName;
    using ned::editor::QuoteStyleName;
    for (const QuoteStyle style : {QuoteStyle::Single, QuoteStyle::Double}) {
        REQUIRE(QuoteStyleByName(QuoteStyleName(style)) == style);
    }
}

TEST_CASE("QuoteStyleByName throws for an unrecognized name", "[FormatRules]") {
    REQUIRE_THROWS_AS(ned::editor::QuoteStyleByName("not-a-real-style"), std::runtime_error);
}

TEST_CASE("RewriteRuleFor(name, language) resolves the language-scoped key first", "[FormatRules]") {
    using ned::editor::QuoteStyle;
    using ned::editor::RewriteRuleFor;
    using ned::editor::SetRewriteQuoteStyle;
    struct Guard {
        ~Guard() {
            SetRewriteQuoteStyle("format-rules-test.capture", std::nullopt);
            SetRewriteQuoteStyle("javascript/format-rules-test.capture", std::nullopt);
        }
    } guard;

    SetRewriteQuoteStyle("format-rules-test.capture", QuoteStyle::Double);
    SetRewriteQuoteStyle("javascript/format-rules-test.capture", QuoteStyle::Single);

    REQUIRE(RewriteRuleFor("format-rules-test.capture", "javascript").quoteStyle == QuoteStyle::Single);
    REQUIRE(RewriteRuleFor("format-rules-test.capture", "python").quoteStyle == QuoteStyle::Double);
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
