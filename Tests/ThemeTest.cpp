#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <sstream>
#include <string>

#include <algorithm>
#include <vector>

#include "Editor/SyntaxTheme.h"
#include "Editor/ThemeSetting.h"
#include "ThemeTestSupport.h"
#include "UI/Compositing.h"
#include "UI/Theme.h"
#include "UI/ThemeFile.h"
#include "UI/ThemeRegistry.h"

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

TEST_CASE("Capture-aware BrushFor: capture chain beats class override beats built-in, field by field", "[Theme]") {
    SyntaxThemeGuard guard;
    struct CaptureGuard {
        ~CaptureGuard() {
            ned::editor::SetCaptureForeground("function", std::nullopt);
            ned::editor::SetCaptureItalic("function.builtin", std::nullopt);
        }
    } captureGuard;
    const Theme theme = DarkTheme();

    // Class tier: the span's own class gets an overridden foreground, so
    // the capture tier below has a real class-level value to beat.
    SetSyntaxForeground(SyntaxClass::FunctionBuiltin, std::string("#101010"));
    struct ClassGuard {
        ~ClassGuard() {
            SetSyntaxForeground(SyntaxClass::FunctionBuiltin, std::nullopt);
        }
    } classGuard;
    // Capture tier: the base name recolors, a middle ancestor sets italic.
    ned::editor::SetCaptureForeground("function", std::string("#202020"));
    ned::editor::SetCaptureItalic("function.builtin", true);

    const auto id    = ned::editor::InternCaptureName("function.builtin.static");
    const auto brush = theme.BrushFor(SyntaxClass::FunctionBuiltin, id);
    // Foreground comes from the capture chain's "function" level, beating
    // the class override; italic from "function.builtin".
    REQUIRE(brush.foreground == Color::RGB(0x20, 0x20, 0x20));
    REQUIRE(brush.italic);
    // Fields nothing set anywhere keep the built-in class value.
    REQUIRE(brush.background == theme.BrushFor(SyntaxClass::FunctionBuiltin).background);

    // kNoCapture degrades to exactly the class-tier result.
    REQUIRE(theme.BrushFor(SyntaxClass::Function, ned::editor::kNoCapture) == theme.BrushFor(SyntaxClass::Function));
}

TEST_CASE("Interpolate returns equal endpoints unchanged, preserving their kind", "[Theme]") {
    // Default has no RGB to interpolate from, so an equal pair must pass
    // through rather than collapsing to a mid-grey approximation.
    REQUIRE(Color::Interpolate(0.5F, Color::Blue, Color::Blue) == Color::Blue);
    REQUIRE(Color::Interpolate(0.0F, Color::Default, Color::Default) == Color::Default);
    // Distinct endpoints still blend to a real TrueColor.
    REQUIRE(Color::Interpolate(0.5F, Color::Blue, Color::Red).kind == Color::Kind::TrueColor);
}

TEST_CASE("Every syntax colour clears a contrast floor against its own background", "[Theme]") {
    // With the ANSI fallback gone, a theme owns its own legibility -- so the
    // floor is checked rather than assumed. A theme whose background is the
    // terminal's own has no RGB to measure against, so it is judged against
    // an assumed backdrop of the same polarity: exactly the
    // "assumedBackground" rule Docs/Translucency.md describes for
    // compositing.
    //
    // Every violation is collected rather than asserted one at a time, so a
    // run reports the whole picture instead of whichever theme happens to
    // sort first -- and so removing a known deviation fails too, prompting
    // the list below to shrink.
    const ned::ui::Color assumedDark  = ned::ui::Color::RGB(0x14141c);
    const ned::ui::Color assumedLight = ned::ui::Color::RGB(0xf0f0ec);

    // Known, deliberate deviations: each is a *cloned* theme carrying the
    // value its upstream palette actually specifies. A clone is a
    // transcription, so these are recorded rather than "corrected" -- see
    // Docs/Themes.md on what cloning means here.
    //
    // solarized-dark's is the one that is arguably ned's own bug rather than
    // the palette's: upstream inverts the text for a search hit instead of
    // painting the buffer's own foreground over the highlight, which is an
    // argument for ned choosing an isearch *foreground*.
    // All eight are the two famously low-contrast *light* clones plus
    // solarized-dark's search highlight. Solarized's body text is 4.13:1
    // against its own background -- below AA, and entirely on purpose: low
    // contrast is that palette's whole thesis, not an oversight to correct
    // in a transcription.
    const std::vector<std::string> knownDeviations = {
        "catppuccin-latte numberForeground",
        "catppuccin-latte stringForeground",
        "catppuccin-latte typeForeground",
        "solarized-dark isearchMatchBackground",
        "solarized-light defaultForeground:AA",
        "solarized-light functionForeground",
        "solarized-light stringForeground",
        "solarized-light typeForeground",
    };

    std::vector<std::string> violations;
    for (const std::string& name : ned::ui::ThemeNames()) {
        const std::optional<Theme> theme = ned::ui::ThemeByName(name);
        REQUIRE(theme.has_value());

        // Polarity comes from the theme's own foreground when its background
        // is the terminal's: light text means a dark backdrop.
        ned::ui::Color background = theme->background;
        if (!background.Composable()) {
            background = ned::tests::Luma(theme->defaultForeground) >= 128 ? assumedDark : assumedLight;
        }

        // 3.0 is the floor for "meant to be read at a glance but allowed to
        // recede" -- comments, line numbers and hints deliberately sit near
        // it. Anything the eye tracks while reading code sits far above.
        const std::pair<const char*, ned::ui::Color> againstBackground[] = {
            {"defaultForeground", theme->defaultForeground},
            {"stringForeground", theme->stringForeground},
            {"keywordForeground", theme->keywordForeground},
            {"numberForeground", theme->numberForeground},
            {"typeForeground", theme->typeForeground},
            {"functionForeground", theme->functionForeground},
            {"operatorForeground", theme->operatorForeground},
        };
        // commentForeground is deliberately not here. A comment's job is to
        // recede, and eleven of the bundled palettes -- nord, one-dark,
        // tokyo-night, zenburn, solarized, catppuccin -- put theirs below
        // 3:1 on purpose. A floor that fails all of them is measuring taste,
        // not legibility.
        //
        // modeLineForeground is checked separately below: it sits on the
        // mode line's own gradient, not on the buffer background, and
        // measuring it against the wrong backdrop was this test's first bug.
        for (const auto& [field, colour] : againstBackground) {
            if (ned::ui::ContrastRatio(colour, background) < 3.0) {
                violations.push_back(name + " " + field);
            }
        }

        for (const auto& [field, against] :
             {std::pair<const char*, ned::ui::Color>{"modeLineForeground:start", theme->modeLineGradientStart},
              std::pair<const char*, ned::ui::Color>{"modeLineForeground:end", theme->modeLineGradientEnd}}) {
            if (!against.Composable() || !theme->modeLineForeground.Composable()) {
                continue;
            }
            if (ned::ui::ContrastRatio(theme->modeLineForeground, against) < 3.0) {
                violations.push_back(name + " " + field);
            }
        }

        // The two theme colours that are a *background* a foreground has to
        // survive, which is why neither is simply the matching syntax hue.
        const std::pair<const char*, ned::ui::Color> underForeground[] = {
            {"selectionBackground", theme->selectionBackground},
            {"isearchMatchBackground", theme->isearchMatchBackground},
        };
        for (const auto& [field, colour] : underForeground) {
            if (!colour.Composable() || !theme->defaultForeground.Composable()) {
                continue; // nothing to measure
            }
            if (ned::ui::ContrastRatio(theme->defaultForeground, colour) < 3.0) {
                violations.push_back(name + " " + field);
            }
        }

        // 4.5 is WCAG AA for body text, and body text is exactly what this
        // is. AAA (7.0) was the first choice and is the wrong bar here: it
        // fails one-dark at 6.6, and a floor that rejects one of the most
        // widely used palettes in the world is measuring the wrong thing.
        if (ned::ui::ContrastRatio(theme->defaultForeground, background) < 4.5) {
            violations.push_back(name + " defaultForeground:AA");
        }
    }

    std::sort(violations.begin(), violations.end());
    for (const std::string& violation : violations) {
        INFO(violation);
    }
    REQUIRE(violations == knownDeviations);
}

TEST_CASE("Every bundled theme is truecolor -- no palette indices survive", "[Theme]") {
    // Phase 3 of Docs/Translucency.md: themes own their own contrast and
    // Notcurses quantizes for a terminal that cannot render them, so a
    // theme field must never be a palette index (it cannot be composited
    // against, and it cannot carry alpha).
    for (const std::string& name : ned::ui::ThemeNames()) {
        const std::optional<Theme> theme = ned::ui::ThemeByName(name);
        REQUIRE(theme.has_value());
        INFO(name);
        for (const auto& [key, token] : ned::tests::SerializedThemeEntries(*theme)) {
            if (ned::tests::IsBrushTraitKey(key)) {
                continue;
            }
            INFO(key << " = " << token);
            REQUIRE_FALSE(token.starts_with("x:"));
        }
    }
}

// rich-theme-set follow-up (Phase 1): the name registry.

TEST_CASE("ThemeByName resolves every registered name to a theme carrying that exact name", "[Theme]") {
    const std::vector<std::string> names = ned::ui::ThemeNames();
    REQUIRE_FALSE(names.empty());

    for (const std::string& name : names) {
        const auto theme = ned::ui::ThemeByName(name);
        REQUIRE(theme.has_value());
        // The table's key and the factory's own .name must agree -- the
        // picker previews by table key and reports theme.name-adjacent
        // strings, so a mismatch would be a real, user-visible confusion.
        REQUIRE(theme->name == name);
    }
}

TEST_CASE("ThemeNames is sorted and covers the hand-built pair; unknown names resolve to nullopt", "[Theme]") {
    const std::vector<std::string> names = ned::ui::ThemeNames();
    REQUIRE(std::is_sorted(names.begin(), names.end()));
    for (const char* expected : {"dark", "light"}) {
        REQUIRE(std::find(names.begin(), names.end(), expected) != names.end());
    }
    // The ANSI fallback pair is gone with the fallback path itself.
    for (const char* removed : {"ansi-dark", "ansi-light"}) {
        REQUIRE(std::find(names.begin(), names.end(), removed) == names.end());
    }
    REQUIRE_FALSE(ned::ui::ThemeByName("no-such-theme").has_value());
}

TEST_CASE("PreferredThemeName round-trips and clears via empty string", "[Theme]") {
    REQUIRE(ned::editor::PreferredThemeName().empty()); // default: no preference

    ned::editor::SetPreferredThemeName("gruvbox-dark");
    REQUIRE(ned::editor::PreferredThemeName() == "gruvbox-dark");

    ned::editor::SetPreferredThemeName("");
    REQUIRE(ned::editor::PreferredThemeName().empty());
}
