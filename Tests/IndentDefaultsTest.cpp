#include <catch2/catch_test_macros.hpp>

#include "Editor/IndentDefaults.h"
#include "Editor/IndentStyle.h"

using ned::editor::BuiltinIndentStyleForLanguage;
using ned::editor::EffectiveIndentStyle;
using ned::editor::IndentStyle;
using ned::editor::SetIndentStyle;

namespace {

// Mirrors IndentStyleTest.cpp's own IndentStyleGuard exactly -- the
// process-wide default is global state that must be restored for the next
// test via RAII, not a manual reset a failed REQUIRE partway through would
// skip.
struct IndentStyleGuard {
    ~IndentStyleGuard() {
        SetIndentStyle(IndentStyle{});
    }
};

} // namespace

TEST_CASE("BuiltinIndentStyleForLanguage returns the documented default for a representative set",
          "[IndentDefaults]") {
    const auto python = BuiltinIndentStyleForLanguage("python");
    REQUIRE(python.has_value());
    REQUIRE_FALSE(python->useTabs);
    REQUIRE(python->width == 4);

    // gofmt mandates literal tabs -- the one entry where useTabs is true.
    const auto go = BuiltinIndentStyleForLanguage("go");
    REQUIRE(go.has_value());
    REQUIRE(go->useTabs);

    const auto javascript = BuiltinIndentStyleForLanguage("javascript");
    REQUIRE(javascript.has_value());
    REQUIRE_FALSE(javascript->useTabs);
    REQUIRE(javascript->width == 2);

    // YAML forbids literal tabs for indentation at all -- confirm the
    // built-in default itself never sets useTabs, whatever a user later
    // overrides it to.
    const auto yaml = BuiltinIndentStyleForLanguage("yaml");
    REQUIRE(yaml.has_value());
    REQUIRE_FALSE(yaml->useTabs);

    // GNU Make requires a literal tab to introduce a recipe line -- the
    // other useTabs=true entry, for a different (syntactic, not stylistic)
    // reason than Go's.
    const auto make = BuiltinIndentStyleForLanguage("make");
    REQUIRE(make.has_value());
    REQUIRE(make->useTabs);
}

TEST_CASE("BuiltinIndentStyleForLanguage returns nullopt for an unknown or unlisted language",
          "[IndentDefaults]") {
    REQUIRE_FALSE(BuiltinIndentStyleForLanguage("indent-defaults-test-not-a-real-language").has_value());
    // Markdown/Org deliberately have no entry -- both use a hand-rolled
    // hanging/outline indent (Mode's own bespoke indentColumn closures), not
    // this engine's flat-width model. See Docs/FormattingRules.md.
    REQUIRE_FALSE(BuiltinIndentStyleForLanguage("markdown").has_value());
    REQUIRE_FALSE(BuiltinIndentStyleForLanguage("org").has_value());
}

TEST_CASE("EffectiveIndentStyle falls through to the built-in per-language default when no per-mode "
          "override is set and the process-wide default differs",
          "[IndentDefaults][IndentStyle]") {
    const IndentStyleGuard guard;
    // Deliberately set the process-wide default to something no bundled
    // language's built-in entry matches, so a pass-through (bug: falling
    // straight to the process-wide default without consulting the new
    // table) would be caught rather than accidentally agreeing anyway.
    SetIndentStyle(IndentStyle{.useTabs = true, .width = 8});

    const IndentStyle python = EffectiveIndentStyle("python-mode");
    REQUIRE_FALSE(python.useTabs);
    REQUIRE(python.width == 4);

    const IndentStyle javascript = EffectiveIndentStyle("javascript-mode");
    REQUIRE_FALSE(javascript.useTabs);
    REQUIRE(javascript.width == 2);

    // A mode name whose stripped language key matches nothing in the table
    // still falls all the way through to the process-wide default.
    const IndentStyle unmapped = EffectiveIndentStyle("indent-defaults-test-unmapped-mode");
    REQUIRE(unmapped.useTabs);
    REQUIRE(unmapped.width == 8);
}
