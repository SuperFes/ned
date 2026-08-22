#include <catch2/catch_test_macros.hpp>

#include <string>

#include "UI/DesktopThemeProbe.h"
#include "UI/Theme.h"

using ned::ui::BuildDesktopTheme;
using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::DesktopThemeInfo;
using ned::ui::GnomeAccentColorFromName;
using ned::ui::KdeGlobalsInfo;
using ned::ui::ParseGsettingsColorScheme;
using ned::ui::ParseKdeGlobals;
using ned::ui::ParsePortalAccentColor;
using ned::ui::ParsePortalColorScheme;
using ned::ui::Theme;

TEST_CASE("ParsePortalColorScheme reads gdbus's <uint32 N> shape", "[DesktopThemeProbe]") {
    REQUIRE(ParsePortalColorScheme("(<uint32 1>,)") == std::optional(true));
    REQUIRE(ParsePortalColorScheme("(<uint32 2>,)") == std::optional(false));
    REQUIRE(ParsePortalColorScheme("(<uint32 0>,)") == std::nullopt);
}

TEST_CASE("ParsePortalColorScheme reads busctl's \"u N\" shape", "[DesktopThemeProbe]") {
    REQUIRE(ParsePortalColorScheme("v u 1\n") == std::optional(true));
    REQUIRE(ParsePortalColorScheme("v u 2\n") == std::optional(false));
}

TEST_CASE("ParsePortalColorScheme returns nullopt for unparseable input", "[DesktopThemeProbe]") {
    REQUIRE(ParsePortalColorScheme("") == std::nullopt);
    REQUIRE(ParsePortalColorScheme("Error: no such method") == std::nullopt);
}

TEST_CASE("ParsePortalAccentColor reads gdbus's comma-separated triple", "[DesktopThemeProbe]") {
    const auto color = ParsePortalAccentColor("(<(0.20000000000000001, 0.40000000000000002, 0.80000000000000004)>,)");
    REQUIRE(color.has_value());
    REQUIRE(color->red == 51);   // round(0.2 * 255)
    REQUIRE(color->green == 102); // round(0.4 * 255)
    REQUIRE(color->blue == 204);  // round(0.8 * 255)
}

TEST_CASE("ParsePortalAccentColor reads busctl's space-separated triple", "[DesktopThemeProbe]") {
    const auto color = ParsePortalAccentColor("v (ddd) 0.2 0.4 0.8\n");
    REQUIRE(color.has_value());
    REQUIRE(color->red == 51);
}

TEST_CASE("ParsePortalAccentColor returns nullopt when no triple is present", "[DesktopThemeProbe]") {
    REQUIRE(ParsePortalAccentColor("(<uint32 1>,)") == std::nullopt);
}

TEST_CASE("ParseGsettingsColorScheme maps GNOME's three known values", "[DesktopThemeProbe]") {
    REQUIRE(ParseGsettingsColorScheme("'prefer-dark'\n") == std::optional(true));
    REQUIRE(ParseGsettingsColorScheme("'prefer-light'\n") == std::optional(false));
    REQUIRE(ParseGsettingsColorScheme("'default'\n") == std::nullopt);
}

TEST_CASE("GnomeAccentColorFromName maps every documented Adwaita accent name", "[DesktopThemeProbe]") {
    const auto blue = GnomeAccentColorFromName("'blue'\n");
    REQUIRE(blue.has_value());
    REQUIRE(*blue == Color::RGB(0x35, 0x84, 0xe4));

    REQUIRE(GnomeAccentColorFromName("'not-a-real-accent'\n") == std::nullopt);
}

TEST_CASE("ParseKdeGlobals reads polarity from a ColorScheme name", "[DesktopThemeProbe]") {
    const std::string content = "[General]\nColorScheme=BreezeDark\nName=Breeze Dark\n";
    const KdeGlobalsInfo info = ParseKdeGlobals(content);
    REQUIRE(info.preferDark == std::optional(true));
}

TEST_CASE("ParseKdeGlobals reads a light ColorScheme name", "[DesktopThemeProbe]") {
    const std::string content = "[General]\nColorScheme=BreezeLight\n";
    REQUIRE(ParseKdeGlobals(content).preferDark == std::optional(false));
}

TEST_CASE("ParseKdeGlobals prefers an explicit AccentColor over Colors:Selection", "[DesktopThemeProbe]") {
    const std::string content =
        "[General]\n"
        "ColorScheme=BreezeDark\n"
        "AccentColor=61,174,233\n"
        "[Colors:Selection]\n"
        "BackgroundNormal=42,42,42\n";
    const KdeGlobalsInfo info = ParseKdeGlobals(content);
    REQUIRE(info.accent == std::optional(Color::RGB(61, 174, 233)));
}

TEST_CASE("ParseKdeGlobals falls back to Colors:Selection when AccentColor is absent", "[DesktopThemeProbe]") {
    const std::string content =
        "[General]\n"
        "ColorScheme=BreezeDark\n"
        "[Colors:Selection]\n"
        "BackgroundNormal=61,174,233\n";
    const KdeGlobalsInfo info = ParseKdeGlobals(content);
    REQUIRE(info.accent == std::optional(Color::RGB(61, 174, 233)));
}

TEST_CASE("ParseKdeGlobals returns an empty result for an unrelated file", "[DesktopThemeProbe]") {
    const KdeGlobalsInfo info = ParseKdeGlobals("[SomeOtherSection]\nKey=Value\n");
    REQUIRE(info.preferDark == std::nullopt);
    REQUIRE(info.accent == std::nullopt);
}

TEST_CASE("BuildDesktopTheme picks Dark/LightTheme by polarity", "[DesktopThemeProbe]") {
    REQUIRE(BuildDesktopTheme(DesktopThemeInfo{.preferDark = true}).name == "dark");
    REQUIRE(BuildDesktopTheme(DesktopThemeInfo{.preferDark = false}).name == "light");
}

TEST_CASE("BuildDesktopTheme applies a given accent to the accent-carrying fields", "[DesktopThemeProbe]") {
    const Color accent = Color::RGB(0x35, 0x84, 0xe4);
    const auto  theme  = BuildDesktopTheme(DesktopThemeInfo{.preferDark = true, .accent = accent});
    REQUIRE(theme.borderAccent.foreground == accent);
    REQUIRE(theme.keywordForeground == accent);
}

TEST_CASE("BuildDesktopTheme without an accent leaves the base theme's accent fields untouched", "[DesktopThemeProbe]") {
    const Theme base   = DarkTheme();
    const auto  theme  = BuildDesktopTheme(DesktopThemeInfo{.preferDark = true});
    REQUIRE(theme.borderAccent.foreground == base.borderAccent.foreground);
    REQUIRE(theme.keywordForeground == base.keywordForeground);
}
