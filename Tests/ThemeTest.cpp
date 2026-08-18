#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/SyntaxTheme.h"
#include "UI/Theme.h"

using ned::editor::SetSyntaxBackground;
using ned::editor::SetSyntaxBold;
using ned::editor::SetSyntaxForeground;
using ned::editor::SetSyntaxItalic;
using ned::editor::SyntaxClass;
using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::LightTheme;
using ned::ui::Theme;

namespace {

// SyntaxTheme overrides are process-wide state (Editor/SyntaxTheme.h) --
// guaranteed reset via RAII so this doesn't leak into other tests.
struct SyntaxThemeGuard {
    ~SyntaxThemeGuard() {
        SetSyntaxForeground(SyntaxClass::Comment, std::nullopt);
        SetSyntaxBackground(SyntaxClass::Comment, std::nullopt);
        SetSyntaxBold(SyntaxClass::Comment, std::nullopt);
        SetSyntaxItalic(SyntaxClass::Comment, std::nullopt);
    }
};

} // namespace

TEST_CASE("Theme::BrushFor maps each syntax class to its themed foreground, sharing one background", "[Theme]") {
    const Theme theme = DarkTheme();

    REQUIRE(theme.BrushFor(SyntaxClass::Default).foreground == theme.defaultForeground);
    REQUIRE(theme.BrushFor(SyntaxClass::Comment).foreground == theme.commentForeground);
    REQUIRE(theme.BrushFor(SyntaxClass::String).foreground == theme.stringForeground);
    REQUIRE(theme.BrushFor(SyntaxClass::Keyword).foreground == theme.keywordForeground);
    REQUIRE(theme.BrushFor(SyntaxClass::Number).foreground == theme.numberForeground);

    for (const auto cls : {SyntaxClass::Default, SyntaxClass::Comment, SyntaxClass::String, SyntaxClass::Keyword, SyntaxClass::Number}) {
        REQUIRE(theme.BrushFor(cls).background == theme.background);
    }
}

TEST_CASE("Theme::BrushFor makes Keyword bold, and nothing else", "[Theme]") {
    const Theme theme = DarkTheme();

    REQUIRE(theme.BrushFor(SyntaxClass::Keyword).bold);
    REQUIRE_FALSE(theme.BrushFor(SyntaxClass::Default).bold);
    REQUIRE_FALSE(theme.BrushFor(SyntaxClass::Comment).bold);
}

TEST_CASE("DarkTheme and LightTheme are distinct palettes", "[Theme]") {
    const Theme dark  = DarkTheme();
    const Theme light = LightTheme();

    REQUIRE(dark.name == "dark");
    REQUIRE(light.name == "light");
    REQUIRE_FALSE(dark.background == light.background);
    REQUIRE_FALSE(dark.defaultForeground == light.defaultForeground);
    REQUIRE_FALSE(dark.selectionBackground == light.selectionBackground);
}

TEST_CASE("BrushFor merges a set override over the built-in value, leaving unset fields alone", "[Theme]") {
    SyntaxThemeGuard guard;
    const Theme      theme    = DarkTheme();
    const auto       original = theme.BrushFor(SyntaxClass::Comment);

    SetSyntaxForeground(SyntaxClass::Comment, std::string("#123456"));
    SetSyntaxBold(SyntaxClass::Comment, true);

    const auto overridden = theme.BrushFor(SyntaxClass::Comment);
    REQUIRE(overridden.foreground == Color::RGB(0x12, 0x34, 0x56));
    REQUIRE(overridden.bold);
    // Untouched fields keep the built-in value exactly.
    REQUIRE(overridden.background == original.background);
    REQUIRE(overridden.italic == original.italic);
}

TEST_CASE("Clearing an override restores the exact original built-in Brush", "[Theme]") {
    SyntaxThemeGuard guard;
    const Theme      theme    = DarkTheme();
    const auto       original = theme.BrushFor(SyntaxClass::Comment);

    SetSyntaxForeground(SyntaxClass::Comment, std::string("#123456"));
    REQUIRE_FALSE(theme.BrushFor(SyntaxClass::Comment) == original);

    SetSyntaxForeground(SyntaxClass::Comment, std::nullopt);
    REQUIRE(theme.BrushFor(SyntaxClass::Comment) == original);
}
