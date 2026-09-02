#include <catch2/catch_test_macros.hpp>

#include "Editor/IndentStyle.h"

using ned::editor::DefaultIndentStyle;
using ned::editor::EffectiveIndentStyle;
using ned::editor::IndentStyle;
using ned::editor::SetIndentStyle;
using ned::editor::SetIndentStyleForMode;

namespace {

// The process-wide default is global state (see IndentStyle.h's own doc
// comment); every test that sets it must restore it for the next test,
// guaranteed via RAII rather than a manual reset at the end (which a failed
// REQUIRE partway through would skip) -- mirrors TabWidthTest.cpp's own
// TabWidthGuard exactly. Per-mode overrides don't need a reset: tests use
// distinct, made-up mode names, the same convention WrapOverridesTest.cpp
// already establishes for its own extension/filename tables.
struct IndentStyleGuard {
    ~IndentStyleGuard() {
        SetIndentStyle(IndentStyle{});
    }
};

} // namespace

TEST_CASE("DefaultIndentStyle defaults to spaces, width 4", "[IndentStyle]") {
    const IndentStyleGuard guard;
    const IndentStyle       style = DefaultIndentStyle();
    REQUIRE_FALSE(style.useTabs);
    REQUIRE(style.width == 4);
}

TEST_CASE("SetIndentStyle/DefaultIndentStyle round-trip", "[IndentStyle]") {
    const IndentStyleGuard guard;
    SetIndentStyle(IndentStyle{.useTabs = true, .width = 8});
    const IndentStyle style = DefaultIndentStyle();
    REQUIRE(style.useTabs);
    REQUIRE(style.width == 8);
}

TEST_CASE("SetIndentStyle clamps a non-positive width to 1", "[IndentStyle]") {
    const IndentStyleGuard guard;
    SetIndentStyle(IndentStyle{.useTabs = false, .width = 0});
    REQUIRE(DefaultIndentStyle().width == 1);
    SetIndentStyle(IndentStyle{.useTabs = false, .width = -5});
    REQUIRE(DefaultIndentStyle().width == 1);
}

TEST_CASE("EffectiveIndentStyle falls back to the process-wide default when no per-mode override is set",
          "[IndentStyle]") {
    const IndentStyleGuard guard;
    SetIndentStyle(IndentStyle{.useTabs = true, .width = 2});
    const IndentStyle style = EffectiveIndentStyle("indent-style-test-unmapped-mode");
    REQUIRE(style.useTabs);
    REQUIRE(style.width == 2);
}

TEST_CASE("SetIndentStyleForMode overrides the default for that mode name only", "[IndentStyle]") {
    const IndentStyleGuard guard;
    SetIndentStyle(IndentStyle{.useTabs = false, .width = 4});
    SetIndentStyleForMode("indent-style-test-python-mode", IndentStyle{.useTabs = true, .width = 8});

    const IndentStyle overridden = EffectiveIndentStyle("indent-style-test-python-mode");
    REQUIRE(overridden.useTabs);
    REQUIRE(overridden.width == 8);

    // A different, unmapped mode name still resolves to the process-wide default.
    const IndentStyle unmapped = EffectiveIndentStyle("indent-style-test-other-mode");
    REQUIRE_FALSE(unmapped.useTabs);
    REQUIRE(unmapped.width == 4);
}

TEST_CASE("SetIndentStyleForMode clamps a non-positive width to 1", "[IndentStyle]") {
    const IndentStyleGuard guard;
    SetIndentStyleForMode("indent-style-test-clamp-mode", IndentStyle{.useTabs = false, .width = 0});
    REQUIRE(EffectiveIndentStyle("indent-style-test-clamp-mode").width == 1);
}
