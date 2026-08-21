#include <catch2/catch_test_macros.hpp>

#include <map>
#include <sstream>
#include <string>

#include "UI/Theme.h"
#include "UI/ThemeFile.h"
#include "UI/ThemePalette.h"

using ned::ui::Color;
using ned::ui::Theme;
using ned::ui::ThemeFromPalette;
using ned::ui::ThemePalette;

namespace {

// Rec. 601 luma, same formula AnsiFallbackFor uses (Theme.cpp) -- good
// enough to order colors by perceived brightness for a contrast floor.
int Luma(const Color& c) {
    REQUIRE(c.kind == Color::Kind::TrueColor); // palette-derived themes are TrueColor throughout
    return (299 * c.red + 587 * c.green + 114 * c.blue) / 1000;
}

std::map<std::string, Color> SerializedColors(const Theme& theme) {
    std::map<std::string, Color> result;
    std::istringstream           in{ned::ui::SerializeTheme(theme)};
    std::string                  line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        REQUIRE(eq != std::string::npos);
        const auto color = ned::ui::ParseColorToken(std::string_view(line).substr(eq + 1));
        REQUIRE(color.has_value());
        result.emplace(line.substr(0, eq), *color);
    }
    return result;
}

// The automated black-on-black guard (rich-theme-set Phase 0): every
// serialized *_foreground field must clear a luma-delta floor against the
// background it actually renders over -- its own Brush's background when
// that Brush sets one, the theme background otherwise. Two special cases:
// mode_line_foreground renders over the gradient, so it's checked against
// both endpoints instead; and the deliberately-quiet chrome (the border
// lines, the disabled scroll bar -- structural marks designed to recede,
// like DarkTheme's own near-background 0x3a3a50 border) gets a third of the
// floor rather than a full skip, so "quiet" can never regress to
// "invisible."
void RequireForegroundContrast(const Theme& theme, int floor) {
    const auto colors = SerializedColors(theme);

    for (const auto& [key, color] : colors) {
        const std::string suffix = "_foreground";
        if (key.size() < suffix.size() || key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        INFO(key);
        const bool quietChrome = key == "scroll_bar_disabled_foreground" || key == "border_foreground";
        const int  keyFloor    = quietChrome ? floor / 3 : floor;
        if (key == "mode_line_foreground") {
            REQUIRE(std::abs(Luma(color) - Luma(colors.at("mode_line_gradient_start"))) >= keyFloor);
            REQUIRE(std::abs(Luma(color) - Luma(colors.at("mode_line_gradient_end"))) >= keyFloor);
            continue;
        }
        Color      against    = theme.background;
        const auto pairedBgIt = colors.find(key.substr(0, key.size() - suffix.size()) + "_background");
        if (pairedBgIt != colors.end() && pairedBgIt->second.kind != Color::Kind::Default) {
            against = pairedBgIt->second;
        }
        REQUIRE(std::abs(Luma(color) - Luma(against)) >= keyFloor);
    }
}

// A One-Dark-flavored sample palette -- realistic values, not the future
// bundled theme itself (that's Phase 2/3 data; this file tests the
// derivation machinery).
ThemePalette SampleDarkPalette() {
    return ThemePalette{
        .background               = Color::RGB(0x282c34),
        .foreground               = Color::RGB(0xabb2bf),
        .subtleForeground         = Color::RGB(0x777d8a),
        .red                      = Color::RGB(0xe06c75),
        .orange                   = Color::RGB(0xd19a66),
        .yellow                   = Color::RGB(0xe5c07b),
        .green                    = Color::RGB(0x98c379),
        .cyan                     = Color::RGB(0x56b6c2),
        .blue                     = Color::RGB(0x61afef),
        .purple                   = Color::RGB(0xc678dd),
        .magenta                  = Color::RGB(0xd55fde),
        .chromeBackground         = Color::RGB(0x21252b),
        .chromeBackgroundEmphasis = Color::RGB(0x2c313a),
        .chromeForeground         = Color::RGB(0xd7dae0),
        .border                   = Color::RGB(0x3e4452),
        .accent                   = Color::RGB(0x8f80e0),
        .selectionBackground      = Color::RGB(0x3e4452),
        .searchMatchBackground    = Color::RGB(0x6b5d24),
    };
}

ThemePalette SampleLightPalette() {
    return ThemePalette{
        .background               = Color::RGB(0xfafafa),
        .foreground               = Color::RGB(0x383a42),
        .subtleForeground         = Color::RGB(0x8a8f98),
        .red                      = Color::RGB(0xca1243),
        .orange                   = Color::RGB(0xb35b00),
        .yellow                   = Color::RGB(0x986801),
        .green                    = Color::RGB(0x50a14f),
        .cyan                     = Color::RGB(0x0184bc),
        .blue                     = Color::RGB(0x4078f2),
        .purple                   = Color::RGB(0xa626a4),
        .magenta                  = Color::RGB(0xc72fbc),
        .chromeBackground         = Color::RGB(0xeaeaeb),
        .chromeBackgroundEmphasis = Color::RGB(0xdbdbdc),
        .chromeForeground         = Color::RGB(0x202227),
        .border                   = Color::RGB(0xc9c9ca),
        .accent                   = Color::RGB(0x6a5acd),
        .selectionBackground      = Color::RGB(0xd0d9e8),
        .searchMatchBackground    = Color::RGB(0xf2d54c),
    };
}

} // namespace

TEST_CASE("ThemeFromPalette maps palette slots to their documented roles", "[ThemePalette]") {
    const ThemePalette p     = SampleDarkPalette();
    const Theme        theme = ThemeFromPalette("sample-dark", p);

    REQUIRE(theme.name == "sample-dark");
    REQUIRE(theme.background == p.background);
    REQUIRE(theme.defaultForeground == p.foreground);
    REQUIRE(theme.commentForeground == p.subtleForeground);
    REQUIRE(theme.stringForeground == p.green);
    REQUIRE(theme.keywordForeground == p.blue);
    REQUIRE(theme.numberForeground == p.magenta);
    REQUIRE(theme.constantForeground == p.purple);
    REQUIRE(theme.functionForeground == p.cyan);
    REQUIRE(theme.typeForeground == p.yellow);
    REQUIRE(theme.operatorForeground == p.red);
    REQUIRE(theme.attributeForeground == p.orange);
    REQUIRE(theme.selectionBackground == p.selectionBackground);
    REQUIRE(theme.isearchMatchBackground == p.searchMatchBackground);
    REQUIRE(theme.truncationIndicatorForeground == p.accent);
    REQUIRE(theme.borderAccent.foreground == p.accent);
    REQUIRE(theme.borderAccent.bold);
    REQUIRE(theme.activeTab.background == p.chromeBackgroundEmphasis);
    REQUIRE(theme.activeTab.bold);
    REQUIRE(theme.modeLineGradientStart == p.chromeBackgroundEmphasis);
    REQUIRE(theme.modeLineGradientEnd == p.chromeBackground);
}

TEST_CASE("ThemeFromPalette derives the focused gradient toward the accent", "[ThemePalette]") {
    const Theme theme = ThemeFromPalette("sample-dark", SampleDarkPalette());

    // Distinct from the base gradient (it's the focus signal), and not the
    // raw accent either (it's a pull toward it, not a replacement).
    REQUIRE_FALSE(theme.modeLineFocusedGradientStart == theme.modeLineGradientStart);
    REQUIRE_FALSE(theme.modeLineFocusedGradientEnd == theme.modeLineGradientEnd);
    REQUIRE_FALSE(theme.modeLineFocusedGradientStart == theme.borderAccent.foreground);
}

TEST_CASE("Palette-derived themes leave no serialized foreground at Default", "[ThemePalette]") {
    // The regression guard for Theme growing a field ThemeFromPalette
    // forgets: aggregate init value-initializes missing trailing fields
    // silently, so catch it here instead of at first paint.
    for (const auto& [key, color] : SerializedColors(ThemeFromPalette("sample-dark", SampleDarkPalette()))) {
        if (key.ends_with("_foreground")) {
            INFO(key);
            REQUIRE(color.kind == Color::Kind::TrueColor);
        }
    }
}

TEST_CASE("Palette-derived themes clear the foreground contrast floor", "[ThemePalette]") {
    RequireForegroundContrast(ThemeFromPalette("sample-dark", SampleDarkPalette()), 40);
    RequireForegroundContrast(ThemeFromPalette("sample-light", SampleLightPalette()), 40);
}
