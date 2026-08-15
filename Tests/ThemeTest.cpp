#include <catch2/catch_test_macros.hpp>

#include "UI/Theme.h"

using ned::editor::SyntaxClass;
using ned::ui::DarkTheme;
using ned::ui::LightTheme;
using ned::ui::Theme;

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
