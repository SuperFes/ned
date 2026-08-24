//
// Shared theme-test helpers (rich-theme-set follow-up, Phase 2) -- the
// contrast-floor guard ThemePaletteTest.cpp introduced in Phase 0, factored
// out once BundledThemesTest.cpp needed it for every bundled palette theme
// too. Catch-macro-based, so only includable from test translation units.
//

#ifndef NED_TESTS_THEMETESTSUPPORT_H
#define NED_TESTS_THEMETESTSUPPORT_H

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

#include "UI/Theme.h"
#include "UI/ThemeFile.h"

namespace ned::tests {

// Rec. 601 luma, same formula AnsiFallbackFor uses (Theme.cpp) -- good
// enough to order colors by perceived brightness for a contrast floor.
inline int Luma(const ui::Color& c) {
    REQUIRE(c.kind == ui::Color::Kind::TrueColor); // palette-derived themes are TrueColor throughout
    return (299 * c.red + 587 * c.green + 114 * c.blue) / 1000;
}

// bold/italic-round-trip follow-up: SerializeTheme's output isn't
// color-only anymore -- each kBrushKeys entry also emits four
// "<prefix>_bold"/"_italic"/"_underlined"/"_strikethrough" "true"/"false"
// lines alongside its color pair (ThemeFile.cpp). Not a color, so
// SerializedColors below skips them rather than asserting every line
// parses as one; the suffix set mirrors ThemeFile.cpp's own closed list.
inline bool IsBrushTraitKey(std::string_view key) {
    return key.ends_with("_bold") || key.ends_with("_italic") || key.ends_with("_underlined") ||
           key.ends_with("_strikethrough");
}

inline std::map<std::string, ui::Color> SerializedColors(const ui::Theme& theme) {
    std::map<std::string, ui::Color> result;
    std::istringstream               in{ui::SerializeTheme(theme)};
    std::string                      line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        REQUIRE(eq != std::string::npos);
        const std::string key = line.substr(0, eq);
        if (IsBrushTraitKey(key)) {
            continue;
        }
        const auto color = ui::ParseColorToken(std::string_view(line).substr(eq + 1));
        REQUIRE(color.has_value());
        result.emplace(key, *color);
    }
    return result;
}

// The automated black-on-black guard (rich-theme-set Phase 0): every
// serialized *_foreground field must clear a luma-delta floor against the
// background it actually renders over -- its own Brush's background when
// that Brush sets one, the theme background otherwise. Two special cases:
// mode_line_foreground renders over the gradient, so it's checked against
// both endpoints instead; and the deliberately-quiet chrome (the border
// lines, the disabled scroll bar, the indent guide -- structural marks
// designed to recede, like DarkTheme's own near-background 0x3a3a50 border)
// gets a third of the floor rather than a full skip, so "quiet" can never
// regress to "invisible."
inline void RequireForegroundContrast(const ui::Theme& theme, int floor) {
    const auto colors = SerializedColors(theme);

    for (const auto& [key, color] : colors) {
        const std::string suffix = "_foreground";
        if (key.size() < suffix.size() || key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        INFO(theme.name << ": " << key);
        const bool quietChrome =
            key == "scroll_bar_disabled_foreground" || key == "border_foreground" || key == "indent_guide_foreground";
        const int keyFloor = quietChrome ? floor / 3 : floor;
        if (key == "mode_line_foreground") {
            REQUIRE(std::abs(Luma(color) - Luma(colors.at("mode_line_gradient_start"))) >= keyFloor);
            REQUIRE(std::abs(Luma(color) - Luma(colors.at("mode_line_gradient_end"))) >= keyFloor);
            continue;
        }
        ui::Color  against    = theme.background;
        const auto pairedBgIt = colors.find(key.substr(0, key.size() - suffix.size()) + "_background");
        if (pairedBgIt != colors.end() && pairedBgIt->second.kind != ui::Color::Kind::Default) {
            against = pairedBgIt->second;
        }
        REQUIRE(std::abs(Luma(color) - Luma(against)) >= keyFloor);
    }
}

} // namespace ned::tests

#endif // NED_TESTS_THEMETESTSUPPORT_H
