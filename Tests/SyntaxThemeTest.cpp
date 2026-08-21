#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/Mode.h"
#include "Editor/SyntaxTheme.h"

using ned::editor::CaptureClassGeneration;
using ned::editor::CaptureNameForId;
using ned::editor::CaptureOverrideFor;
using ned::editor::InternCaptureName;
using ned::editor::kNoCapture;
using ned::editor::ResolvedCaptureOverride;
using ned::editor::SetCaptureBold;
using ned::editor::SetCaptureForeground;
using ned::editor::SetCaptureItalic;
using ned::editor::SetSyntaxBackground;
using ned::editor::SetSyntaxBold;
using ned::editor::SetSyntaxClassForCapture;
using ned::editor::SetSyntaxForeground;
using ned::editor::SetSyntaxItalic;
using ned::editor::SetSyntaxStrikethrough;
using ned::editor::SetSyntaxUnderlined;
using ned::editor::SyntaxClass;
using ned::editor::SyntaxClassByName;
using ned::editor::SyntaxClassName;
using ned::editor::SyntaxClassNames;
using ned::editor::SyntaxClassOverrideForCapture;
using ned::editor::SyntaxOverrideFor;
using ned::editor::SyntaxThemeGeneration;

namespace {

// Every override set by these tests is cleared afterward, guaranteed via
// RAII -- this is process-wide state (see SyntaxTheme.h), the same
// "guaranteed reset so this doesn't leak into other tests" precedent
// TabWidthTest.cpp/other process-wide-state tests already establish.
struct SyntaxThemeGuard {
    ~SyntaxThemeGuard() {
        SetSyntaxForeground(SyntaxClass::Comment, std::nullopt);
        SetSyntaxBackground(SyntaxClass::Comment, std::nullopt);
        SetSyntaxBold(SyntaxClass::Comment, std::nullopt);
        SetSyntaxItalic(SyntaxClass::Comment, std::nullopt);
        SetSyntaxUnderlined(SyntaxClass::Comment, std::nullopt);
        SetSyntaxStrikethrough(SyntaxClass::Comment, std::nullopt);
    }
};

} // namespace

TEST_CASE("SyntaxClassByName/SyntaxClassName round-trip for every real SyntaxClass member", "[SyntaxTheme]") {
    for (const std::string& name : SyntaxClassNames()) {
        const SyntaxClass cls = SyntaxClassByName(name);
        REQUIRE(SyntaxClassName(cls) == name);
    }
    // A real enumeration check, not spot checks -- confirms the name table
    // actually covers the whole enum, not just a subset.
    REQUIRE(SyntaxClassNames().size() >= 41); // every SyntaxClass member (Mode.h) as of this writing
}

TEST_CASE("SyntaxClassByName throws for an unrecognized name", "[SyntaxTheme]") {
    REQUIRE_THROWS_AS(SyntaxClassByName("not-a-real-class"), std::runtime_error);
}

TEST_CASE("A class with no override has every field unset", "[SyntaxTheme]") {
    const auto override = SyntaxOverrideFor(SyntaxClass::Comment);
    REQUIRE_FALSE(override.foreground.has_value());
    REQUIRE_FALSE(override.background.has_value());
    REQUIRE_FALSE(override.bold.has_value());
    REQUIRE_FALSE(override.italic.has_value());
    REQUIRE_FALSE(override.underlined.has_value());
    REQUIRE_FALSE(override.strikethrough.has_value());
}

TEST_CASE("Setting and clearing a foreground override round-trips and bumps the generation", "[SyntaxTheme]") {
    SyntaxThemeGuard guard;

    const std::size_t generationBefore = SyntaxThemeGeneration();
    SetSyntaxForeground(SyntaxClass::Comment, std::string("#5c6370"));
    REQUIRE(SyntaxThemeGeneration() > generationBefore);
    REQUIRE(SyntaxOverrideFor(SyntaxClass::Comment).foreground == "#5c6370");

    SetSyntaxForeground(SyntaxClass::Comment, std::nullopt);
    REQUIRE_FALSE(SyntaxOverrideFor(SyntaxClass::Comment).foreground.has_value());
}

TEST_CASE("Setting a malformed hex color throws", "[SyntaxTheme]") {
    SyntaxThemeGuard guard;
    REQUIRE_THROWS_AS(SetSyntaxForeground(SyntaxClass::Comment, std::string("not-a-color")), std::runtime_error);
    REQUIRE_THROWS_AS(SetSyntaxForeground(SyntaxClass::Comment, std::string("#fff")), std::runtime_error);    // too short
    REQUIRE_THROWS_AS(SetSyntaxBackground(SyntaxClass::Comment, std::string("#gggggg")), std::runtime_error); // not hex digits
}

namespace {

// Same guaranteed-reset shape as SyntaxThemeGuard above, for the
// per-capture stores (exhaustive-highlighting follow-up) -- capture names
// used by these tests only. Interning itself is append-only by design and
// needs no reset (an interned name with no override configured styles
// nothing).
struct CaptureThemeGuard {
    ~CaptureThemeGuard() {
        for (const char* name : {"function", "function.builtin", "function.builtin.static"}) {
            SetCaptureForeground(name, std::nullopt);
            SetCaptureBold(name, std::nullopt);
            SetCaptureItalic(name, std::nullopt);
            SetSyntaxClassForCapture(name, std::nullopt);
        }
    }
};

} // namespace

TEST_CASE("InternCaptureName is stable per name and CaptureNameForId round-trips", "[SyntaxTheme]") {
    const auto id = InternCaptureName("function.builtin");
    REQUIRE(id != kNoCapture);
    REQUIRE(InternCaptureName("function.builtin") == id);
    REQUIRE(CaptureNameForId(id) == "function.builtin");
    REQUIRE(InternCaptureName("comment") != id);
    REQUIRE(CaptureNameForId(kNoCapture).empty());
}

TEST_CASE("ResolvedCaptureOverride walks the dotted chain, most specific field first", "[SyntaxTheme]") {
    CaptureThemeGuard guard;

    SetCaptureForeground("function", std::string("#111111"));
    SetCaptureBold("function.builtin", true);
    SetCaptureForeground("function.builtin.static", std::string("#222222"));

    // The exact name takes its own foreground, inherits bold from the
    // middle level; nothing set italic anywhere.
    const auto resolved = ResolvedCaptureOverride("function.builtin.static");
    REQUIRE(resolved.foreground == "#222222");
    REQUIRE(resolved.bold == true);
    REQUIRE_FALSE(resolved.italic.has_value());

    // The middle name inherits foreground from the base, keeps its own bold.
    const auto middle = ResolvedCaptureOverride("function.builtin");
    REQUIRE(middle.foreground == "#111111");
    REQUIRE(middle.bold == true);

    // A sibling specific name nothing configured falls back to the base.
    const auto sibling = ResolvedCaptureOverride("function.call");
    REQUIRE(sibling.foreground == "#111111");
    REQUIRE_FALSE(sibling.bold.has_value());
}

TEST_CASE("CaptureOverrideFor is exact-name only, no inheritance", "[SyntaxTheme]") {
    CaptureThemeGuard guard;

    SetCaptureForeground("function", std::string("#111111"));
    REQUIRE_FALSE(CaptureOverrideFor("function.builtin").foreground.has_value());
    REQUIRE(CaptureOverrideFor("function").foreground == "#111111");
}

TEST_CASE("A malformed capture name throws; an unknown well-formed one doesn't", "[SyntaxTheme]") {
    CaptureThemeGuard guard;

    REQUIRE_THROWS_AS(SetCaptureForeground("", std::string("#111111")), std::runtime_error);
    REQUIRE_THROWS_AS(SetCaptureForeground("@function", std::string("#111111")), std::runtime_error);
    REQUIRE_THROWS_AS(SetCaptureForeground(".function", std::string("#111111")), std::runtime_error);
    REQUIRE_THROWS_AS(SetCaptureForeground("function.", std::string("#111111")), std::runtime_error);
    REQUIRE_THROWS_AS(SetCaptureForeground("function..builtin", std::string("#111111")), std::runtime_error);
    REQUIRE_THROWS_AS(SetCaptureForeground("has space", std::string("#111111")), std::runtime_error);
    REQUIRE_THROWS_AS(SetCaptureForeground("function", std::string("not-a-color")), std::runtime_error);
    // Unknown but well-formed: configurable before any grammar produces it.
    REQUIRE_NOTHROW(SetCaptureForeground("function.builtin.static", std::string("#333333")));
}

TEST_CASE("Capture setters bump SyntaxThemeGeneration, remaps bump CaptureClassGeneration", "[SyntaxTheme]") {
    CaptureThemeGuard guard;

    const std::size_t styleBefore = SyntaxThemeGeneration();
    const std::size_t classBefore = CaptureClassGeneration();

    SetCaptureBold("function", true);
    REQUIRE(SyntaxThemeGeneration() > styleBefore);
    REQUIRE(CaptureClassGeneration() == classBefore);

    SetSyntaxClassForCapture("function", SyntaxClass::Comment);
    REQUIRE(CaptureClassGeneration() > classBefore);
    REQUIRE(SyntaxClassOverrideForCapture("function") == SyntaxClass::Comment);

    SetSyntaxClassForCapture("function", std::nullopt);
    REQUIRE_FALSE(SyntaxClassOverrideForCapture("function").has_value());
}

TEST_CASE("Bold/italic/underlined/strikethrough overrides round-trip independently", "[SyntaxTheme]") {
    SyntaxThemeGuard guard;

    SetSyntaxBold(SyntaxClass::Comment, true);
    SetSyntaxItalic(SyntaxClass::Comment, false);
    SetSyntaxUnderlined(SyntaxClass::Comment, true);
    SetSyntaxStrikethrough(SyntaxClass::Comment, false);

    const auto override = SyntaxOverrideFor(SyntaxClass::Comment);
    REQUIRE(override.bold == true);
    REQUIRE(override.italic == false);
    REQUIRE(override.underlined == true);
    REQUIRE(override.strikethrough == false);
    // Only the four trait fields were touched -- colors stay unset.
    REQUIRE_FALSE(override.foreground.has_value());
    REQUIRE_FALSE(override.background.has_value());
}
