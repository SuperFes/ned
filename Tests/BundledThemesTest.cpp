#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "ThemeTestSupport.h"
#include "UI/Theme.h"
#include "UI/ThemeRegistry.h"

using ned::tests::Luma;
using ned::tests::RequireForegroundContrast;
using ned::tests::SerializedColors;
using ned::ui::Color;
using ned::ui::Theme;
using ned::ui::ThemeByName;
using ned::ui::ThemeNames;

// rich-theme-set follow-up (Phase 2): the eight original palette-derived
// themes, tested through the registry by name only -- the same way every
// real consumer (the picker, ned/set-theme) reaches them.

namespace {

const std::vector<std::string> kOriginals = {
    "major-dark",
    "major-light",
    "minor-dark",
    "minor-light",
    "mono-dark",
    "mono-light",
    "high-contrast-dark",
    "high-contrast-light",
    "fuchsia", // user-requested signature-hue dark theme (post-Phase-3)
};

// rich-theme-set Phase 3: the cloned set (see ThemeRegistry.cpp's own
// attribution comments for each palette's upstream source).
const std::vector<std::string> kClones = {
    "solarized-dark",
    "solarized-light",
    "gruvbox-dark",
    "gruvbox-light",
    "nord",
    "dracula",
    "monokai",
    "one-dark",
    "one-light",
    "catppuccin-mocha",
    "catppuccin-latte",
    "tokyo-night",
    "tokyo-night-day",
};

// rich-theme-set Phase 4: additional flavors of an already-shipped family,
// plus three new single-flavor clones (ThemeRegistry.cpp has each one's
// upstream attribution).
const std::vector<std::string> kPhase4Clones = {
    "rose-pine",
    "everforest",
    "zenburn",
    "catppuccin-frappe",
    "catppuccin-macchiato",
    "tokyo-night-storm",
};

Theme Resolve(const std::string& name) {
    const auto theme = ThemeByName(name);
    REQUIRE(theme.has_value());
    return *theme;
}

} // namespace

TEST_CASE("The original set is registered under their own names", "[BundledThemes]") {
    const std::vector<std::string> names = ThemeNames();
    for (const std::string& name : kOriginals) {
        INFO(name);
        REQUIRE(std::find(names.begin(), names.end(), name) != names.end());
        REQUIRE(Resolve(name).name == name);
    }
}

TEST_CASE("Every bundled palette theme clears the standard contrast floor", "[BundledThemes]") {
    for (const std::string& name : kOriginals) {
        RequireForegroundContrast(Resolve(name), 40);
    }
    for (const std::string& name : kClones) {
        RequireForegroundContrast(Resolve(name), 40);
    }
    for (const std::string& name : kPhase4Clones) {
        RequireForegroundContrast(Resolve(name), 40);
    }
}

TEST_CASE("The cloned themes are all registered under their own names", "[BundledThemes]") {
    const std::vector<std::string> names = ThemeNames();
    for (const std::string& name : kClones) {
        INFO(name);
        REQUIRE(std::find(names.begin(), names.end(), name) != names.end());
        REQUIRE(Resolve(name).name == name);
    }
    for (const std::string& name : kPhase4Clones) {
        INFO(name);
        REQUIRE(std::find(names.begin(), names.end(), name) != names.end());
        REQUIRE(Resolve(name).name == name);
    }
}

TEST_CASE("Cloned dark/light pairs sit on opposite background polarities", "[BundledThemes]") {
    for (const std::string& family : {std::string("solarized"), std::string("gruvbox"), std::string("one")}) {
        INFO(family);
        REQUIRE(Luma(Resolve(family + "-dark").background) < 128);
        REQUIRE(Luma(Resolve(family + "-light").background) >= 128);
    }
    // The pairs whose flavor names don't follow the -dark/-light convention.
    REQUIRE(Luma(Resolve("catppuccin-mocha").background) < 128);
    REQUIRE(Luma(Resolve("catppuccin-latte").background) >= 128);
    REQUIRE(Luma(Resolve("tokyo-night").background) < 128);
    REQUIRE(Luma(Resolve("tokyo-night-day").background) >= 128);
}

TEST_CASE("Every Phase 4 clone is a dark background", "[BundledThemes]") {
    // All six are dark-only flavors (Everforest/Zenburn/Rosé Pine have no
    // bundled light counterpart; Catppuccin Frappé/Macchiato and Tokyo
    // Night Storm slot into already-dark families).
    for (const std::string& name : kPhase4Clones) {
        INFO(name);
        REQUIRE(Luma(Resolve(name).background) < 128);
    }
}

TEST_CASE("The high-contrast pair clears a raised contrast floor", "[BundledThemes]") {
    // 90 (vs. the standard 40) is what makes "high contrast" a tested
    // property rather than a name -- roughly "every foreground sits in the
    // opposite third of the luma range from its background."
    RequireForegroundContrast(Resolve("high-contrast-dark"), 90);
    RequireForegroundContrast(Resolve("high-contrast-light"), 90);
}

TEST_CASE("The monochrome pair is genuinely grayscale in every serialized color", "[BundledThemes]") {
    // Covers the Interpolate-derived in-between shades too (dim tab text,
    // execution-line wash, focused gradient) -- blending grays yields gray,
    // and this is the regression guard keeping it that way if a hue ever
    // sneaks into a mono palette slot.
    for (const std::string& name : {std::string("mono-dark"), std::string("mono-light")}) {
        for (const auto& [key, color] : SerializedColors(Resolve(name))) {
            INFO(name << ": " << key);
            if (color.kind == Color::Kind::Default) {
                continue; // a deliberate pass-through (the scroll bar's unset Brush background), not a hue
            }
            REQUIRE(color.kind == Color::Kind::TrueColor);
            REQUIRE(color.red == color.green);
            REQUIRE(color.green == color.blue);
        }
    }
}

TEST_CASE("Dark and light variants of each pair sit on opposite background polarities", "[BundledThemes]") {
    for (const std::string& family : {std::string("major"), std::string("minor"), std::string("mono"),
                                      std::string("high-contrast")}) {
        INFO(family);
        REQUIRE(Luma(Resolve(family + "-dark").background) < 128);
        REQUIRE(Luma(Resolve(family + "-light").background) >= 128);
    }
}
