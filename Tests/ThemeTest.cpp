#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <sstream>
#include <string>

#include "Editor/SyntaxTheme.h"
#include "UI/Theme.h"
#include "UI/ThemeFile.h"

using ned::editor::SetSyntaxBackground;
using ned::editor::SetSyntaxBold;
using ned::editor::SetSyntaxForeground;
using ned::editor::SetSyntaxItalic;
using ned::editor::SyntaxClass;
using ned::ui::AnsiDarkTheme;
using ned::ui::AnsiFallbackFor;
using ned::ui::AnsiLightTheme;
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

namespace {

// Walks every SerializeTheme'd color token and fails on anything outside
// the ANSI themes' documented restriction (Theme.h): palette 0-7 or
// "default", never a "#rrggbb" TrueColor and never the Bright 8-15 range.
void RequireAnsiRestricted(const Theme& theme) {
    std::istringstream in{ned::ui::SerializeTheme(theme)};
    std::string        line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        REQUIRE(eq != std::string::npos);
        const std::string token = line.substr(eq + 1);
        INFO(line);
        if (token == "default") {
            continue;
        }
        REQUIRE(token.starts_with("x:"));
        REQUIRE(std::stoi(token.substr(2)) <= 7);
    }
}

} // namespace

TEST_CASE("ANSI fallback themes use only palette 0-7 and default colors", "[Theme]") {
    RequireAnsiRestricted(AnsiDarkTheme());
    RequireAnsiRestricted(AnsiLightTheme());

    // markupMarkerForeground is the one Color field SerializeTheme doesn't
    // cover -- checked directly so the restriction genuinely holds
    // theme-wide.
    for (const Theme& theme : {AnsiDarkTheme(), AnsiLightTheme()}) {
        REQUIRE(theme.markupMarkerForeground.kind != Color::Kind::TrueColor);
        REQUIRE(theme.markupMarkerForeground.paletteIndex <= 7);
    }
}

TEST_CASE("ANSI fallback themes flatten both mode-line gradients", "[Theme]") {
    for (const Theme& theme : {AnsiDarkTheme(), AnsiLightTheme()}) {
        REQUIRE(theme.modeLineGradientStart == theme.modeLineGradientEnd);
        REQUIRE(theme.modeLineFocusedGradientStart == theme.modeLineFocusedGradientEnd);
    }
}

TEST_CASE("Interpolate returns equal endpoints unchanged, preserving their kind", "[Theme]") {
    // The property the flattened gradients above rely on: a Palette16
    // endpoint must not degrade to its TrueColor approximation.
    REQUIRE(Color::Interpolate(0.5F, Color::Blue, Color::Blue) == Color::Blue);
    REQUIRE(Color::Interpolate(0.0F, Color::Default, Color::Default) == Color::Default);
    // Distinct endpoints still blend to a real TrueColor.
    REQUIRE(Color::Interpolate(0.5F, Color::Blue, Color::Red).kind == Color::Kind::TrueColor);
}

TEST_CASE("AnsiFallbackFor picks the variant matching the theme's polarity", "[Theme]") {
    REQUIRE(AnsiFallbackFor(DarkTheme()).name == "ansi-dark");   // Default background
    REQUIRE(AnsiFallbackFor(LightTheme()).name == "ansi-light"); // light TrueColor background

    Theme detectedDark      = DarkTheme();
    detectedDark.background = Color::RGB(0x1e1e2e); // a --detect-theme file from a dark terminal
    REQUIRE(AnsiFallbackFor(detectedDark).name == "ansi-dark");
}
