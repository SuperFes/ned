#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "UI/Compositing.h"
#include "UI/Paint.h"
#include "UI/PaintParse.h"
#include "UI/Theme.h"
#include "UI/ThemeFile.h"
#include "UI/ThemePaints.h"

using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::GradientAt;
using ned::ui::Paint;
using ned::ui::PaintAxis;
using ned::ui::PaintContext;
using ned::ui::PaintKind;
using ned::ui::PaintToken;
using ned::ui::PaintToString;
using ned::ui::ParseColorStop;
using ned::ui::ParsePaint;
using ned::ui::PatternKind;
using ned::ui::Surface;
using ned::ui::Theme;

namespace {

// Named paints and surface overrides are process-wide (the same
// mutex-guarded-static shape every editor-wide setting uses), so tests that
// touch them clean up after themselves.
struct PaintRegistryGuard {
    ~PaintRegistryGuard() {
        ned::ui::ClearNamedPaints();
        ned::ui::ClearSurfaceOverrides();
    }
};

PaintContext ContextWithSlots() {
    static const Theme theme = DarkTheme();
    return ned::ui::PaintContextFor(theme);
}

std::optional<Paint> Parse(std::string_view line, std::string* error = nullptr) {
    return ParsePaint(line, ContextWithSlots(), error);
}

} // namespace

TEST_CASE("A single colour is a solid", "[PaintParse]") {
    const auto paint = Parse("#1e1e2e");
    REQUIRE(paint.has_value());
    REQUIRE(paint->kind == PaintKind::Solid);
    REQUIRE(paint->stops.size() == 1);
    REQUIRE(paint->stops.front().colour == Color::RGB(0x1e1e2e));
}

TEST_CASE("Strings are stops and numbers are weights -- the whole grammar", "[PaintParse]") {
    const auto paint = Parse("y #89b4faff 3 #89b4fa00");
    REQUIRE(paint.has_value());
    REQUIRE(paint->kind == PaintKind::Gradient);
    REQUIRE(paint->axis == PaintAxis::Y);
    REQUIRE(paint->stops.size() == 2);
    REQUIRE(paint->stops[0].colour.alpha == 255);
    REQUIRE(paint->stops[1].colour.alpha == 0);
    REQUIRE(paint->stops[1].weight == 3.0F); // the weight belongs to the span ending at it

    SECTION("and the weight actually moves the ramp") {
        // 3:1 means three quarters of the run is still the first colour's side.
        REQUIRE(static_cast<int>(GradientAt(paint->stops, 0.75).alpha) == 64);
    }
}

TEST_CASE("The axis defaults to y and is named without its keyword marker", "[PaintParse]") {
    REQUIRE(Parse("#000000 #ffffff")->axis == PaintAxis::Y);
    REQUIRE(Parse("x #000000 #ffffff")->axis == PaintAxis::X);
    REQUIRE(Parse(":x #000000 #ffffff")->axis == PaintAxis::X); // as a scripting keyword arrives
    REQUIRE(Parse("diag #000000 #ffffff")->axis == PaintAxis::Diag);
    REQUIRE(Parse("radial #000000 #ffffff")->axis == PaintAxis::Radial);
}

TEST_CASE("#rrggbbaa carries alpha, #rrggbb stays opaque", "[PaintParse]") {
    REQUIRE(Parse("#11223380")->stops.front().colour.alpha == 0x80);
    REQUIRE(Parse("#112233")->stops.front().colour.alpha == 255);
}

TEST_CASE("A percentage stop makes it a Fade", "[PaintParse]") {
    const auto paint = Parse("x 100% 0%");
    REQUIRE(paint.has_value());
    REQUIRE(paint->kind == PaintKind::Fade);
    REQUIRE(paint->fades.size() == 2);
    REQUIRE(paint->fades[0].amount == 1.0F);
    REQUIRE(paint->fades[1].amount == 0.0F);
}

TEST_CASE("Mixing colours and percentages is an error, not a guess", "[PaintParse]") {
    std::string error;
    REQUIRE_FALSE(Parse("x #ffffff 50%", &error).has_value());
    REQUIRE(error.find("never both") != std::string::npos);
}

TEST_CASE("Slot references resolve against the theme's own fields", "[PaintParse]") {
    const Theme theme = DarkTheme();
    const auto  paint = Parse("$fg");
    REQUIRE(paint.has_value());
    REQUIRE(paint->stops.front().colour == theme.defaultForeground);

    SECTION("an unknown slot fails rather than painting something arbitrary") {
        REQUIRE_FALSE(Parse("$nope").has_value());
    }
}

TEST_CASE("Slot adjustments lighten, darken and fade", "[PaintParse]") {
    const PaintContext context = ContextWithSlots();
    const Theme        theme   = DarkTheme();

    const auto lifted = ParseColorStop("$keyword+20", context);
    REQUIRE(lifted.has_value());
    REQUIRE(ned::ui::RelativeLuminance(*lifted) > ned::ui::RelativeLuminance(theme.keywordForeground));

    const auto darkened = ParseColorStop("$keyword-20", context);
    REQUIRE(darkened.has_value());
    REQUIRE(ned::ui::RelativeLuminance(*darkened) < ned::ui::RelativeLuminance(theme.keywordForeground));

    const auto faded = ParseColorStop("$keyword/60", context);
    REQUIRE(faded.has_value());
    REQUIRE(static_cast<int>(faded->alpha) == 153); // 60% of 255

    SECTION("adjustments chain left to right, and lightness does not disturb alpha") {
        const auto both = ParseColorStop("$keyword+8/60", context);
        REQUIRE(both.has_value());
        REQUIRE(static_cast<int>(both->alpha) == 153);
    }

    SECTION("a malformed adjustment is rejected") {
        REQUIRE_FALSE(ParseColorStop("$keyword+", context).has_value());
        REQUIRE_FALSE(ParseColorStop("$keyword*4", context).has_value());
    }
}

TEST_CASE("A stop naming a paint expands to that paint's stops", "[PaintParse]") {
    const PaintRegistryGuard guard;
    ned::ui::RegisterNamedPaint("brand", *Parse("diag #40c080 2 #2050c0"));

    SECTION("used alone") {
        const auto paint = Parse("$brand");
        REQUIRE(paint.has_value());
        REQUIRE(paint->stops.size() == 2);
    }

    SECTION("re-axed") {
        const auto paint = Parse("x $brand");
        REQUIRE(paint.has_value());
        REQUIRE(paint->axis == PaintAxis::X);
        REQUIRE(paint->stops.size() == 2);
    }

    SECTION("extended with a settling stop") {
        const auto paint = Parse("y $brand #000000");
        REQUIRE(paint.has_value());
        REQUIRE(paint->stops.size() == 3);
    }
}

TEST_CASE("A pattern keyword takes one number, its period", "[PaintParse]") {
    const auto paint = Parse("checker 2 #000000 #ffffff");
    REQUIRE(paint.has_value());
    REQUIRE(paint->kind == PaintKind::Pattern);
    REQUIRE(paint->pattern == PatternKind::Checker);
    REQUIRE(paint->period == 2);
    REQUIRE(paint->stops.size() == 2);

    SECTION("later numbers are still weights -- the duty cycle") {
        const auto duty = Parse("stripes 6 #000000 3 #ffffff");
        REQUIRE(duty.has_value());
        REQUIRE(duty->period == 6);
        REQUIRE(duty->stops[1].weight == 3.0F);
    }

    SECTION("patterns decline glyph cells by default") {
        REQUIRE(paint->policy == ned::ui::AlphaPolicy::Dither);
    }
}

TEST_CASE("Blur takes a radius and an optional tint", "[PaintParse]") {
    const auto paint = Parse("blur 2 #00000033");
    REQUIRE(paint.has_value());
    REQUIRE(paint->kind == PaintKind::Blur);
    REQUIRE(paint->period == 2);
    REQUIRE(paint->stops.front().colour.alpha == 0x33);
}

TEST_CASE("Stack needs nesting, which the one-line form cannot express", "[PaintParse]") {
    const PaintRegistryGuard guard;
    ned::ui::RegisterNamedPaint("glass", *Parse("y #1e1e2ec0 #1e1e2e80"));

    SECTION("nested arrays, as a scripting language would send them") {
        const std::vector<PaintToken> tokens = {
            PaintToken::Str(":stack"),
            PaintToken::Sub({PaintToken::Str("$glass")}),
            PaintToken::Sub({PaintToken::Str("noise"), PaintToken::Num(6), PaintToken::Str("#20202080")}),
        };
        std::string error;
        const auto  paint = ParsePaint(tokens, ContextWithSlots(), &error);
        REQUIRE(paint.has_value());
        REQUIRE(paint->kind == PaintKind::Stack);
        REQUIRE(paint->layers.size() == 2);
        REQUIRE(paint->layers[1].kind == PaintKind::Pattern);
    }

    SECTION("a bare named paint is a legal layer") {
        const std::vector<PaintToken> tokens = {PaintToken::Str(":stack"), PaintToken::Str("$glass")};
        REQUIRE(ParsePaint(tokens, ContextWithSlots())->layers.size() == 1);
    }

    SECTION("a weight where a layer belongs is an error") {
        const std::vector<PaintToken> tokens = {PaintToken::Str(":stack"), PaintToken::Num(3)};
        std::string                   error;
        REQUIRE_FALSE(ParsePaint(tokens, ContextWithSlots(), &error).has_value());
        REQUIRE_FALSE(error.empty());
    }
}

TEST_CASE("Malformed paints report why", "[PaintParse]") {
    std::string error;
    REQUIRE_FALSE(Parse("", &error).has_value());
    REQUIRE_FALSE(Parse("x", &error).has_value()); // an axis with no stops
    REQUIRE_FALSE(Parse("x #ffffff -3 #000000", &error).has_value());
    REQUIRE(error.find("weight") != std::string::npos);
    REQUIRE_FALSE(Parse("x not-a-colour", &error).has_value());
    REQUIRE(error.find("not-a-colour") != std::string::npos);
}

TEST_CASE("PaintToString round-trips the one-line form", "[PaintParse]") {
    for (const char* line : {"#1e1e2e", "x #000000 #ffffff", "y #89b4faff 3 #89b4fa00", "x 100% 25%",
                             "checker 2 #000000 #ffffff", "blur 2 #00000033"}) {
        const auto paint = Parse(line);
        REQUIRE(paint.has_value());
        INFO(line);
        const auto reparsed = Parse(PaintToString(*paint));
        REQUIRE(reparsed.has_value());
        REQUIRE(PaintToString(*reparsed) == PaintToString(*paint));
    }

    SECTION("a stack has no one-line form and says so by coming back empty") {
        const std::vector<PaintToken> tokens = {PaintToken::Str(":stack"),
                                                PaintToken::Sub({PaintToken::Str("#101010")})};
        REQUIRE(PaintToString(*ParsePaint(tokens, ContextWithSlots())).empty());
    }
}

TEST_CASE("Surfaces default to what the widget paints today", "[PaintParse]") {
    const PaintRegistryGuard guard;
    const Theme              theme = DarkTheme();

    const Surface modeline = ned::ui::SurfaceFor(theme, "modeline");
    REQUIRE(modeline.fill.kind == PaintKind::Gradient);
    REQUIRE(modeline.fill.stops.front().colour == theme.modeLineGradientStart);
    REQUIRE(modeline.fill.stops.back().colour == theme.modeLineGradientEnd);

    SECTION("the current line has no default highlight -- ned has never drawn one") {
        const Surface current = ned::ui::SurfaceFor(theme, "buffer.current_line");
        REQUIRE(current.fill.stops.empty());
        REQUIRE(current.text.stops.empty());
    }

    SECTION("an unknown surface paints nothing") {
        REQUIRE(ned::ui::SurfaceFor(theme, "no.such.surface").fill.stops.empty());
    }
}

TEST_CASE("A surface override wins over the derived default", "[PaintParse]") {
    const PaintRegistryGuard guard;
    const Theme              theme = DarkTheme();

    Surface popup;
    popup.fill = *Parse("y #1e1e2ec0 #1e1e2e80");
    ned::ui::SetSurfaceOverride("popup", popup);

    REQUIRE(ned::ui::SurfaceFor(theme, "popup").fill.kind == PaintKind::Gradient);
    REQUIRE(ned::ui::SurfaceFor(theme, "popup").fill.stops.front().colour.alpha == 0xC0);
    REQUIRE(ned::ui::SurfaceOverrideNames() == std::vector<std::string>{"popup"});
}

TEST_CASE("Every named surface resolves, and every slot names a real colour", "[PaintParse]") {
    const Theme theme = DarkTheme();
    for (const std::string& name : ned::ui::SurfaceNames()) {
        INFO(name);
        REQUIRE_NOTHROW(ned::ui::SurfaceFor(theme, name));
    }
    const PaintContext context = ned::ui::PaintContextFor(theme);
    for (const std::string& slot : ned::ui::ThemeSlots()) {
        INFO(slot);
        REQUIRE(ParseColorStop("$" + slot, context).has_value());
    }
}

TEST_CASE("save-theme round-trips named paints and surface overrides", "[PaintParse]") {
    const PaintRegistryGuard guard;
    const Theme              theme = DarkTheme();

    ned::ui::RegisterNamedPaint("brand", *Parse("diag #40c080 2 #2050c0"));

    Surface popup;
    popup.fill   = *Parse("y #1e1e2ec0 3 #1e1e2e80");
    popup.border = *Parse("$brand");
    ned::ui::SetSurfaceOverride("popup", popup);

    const std::string janet = ned::ui::SerializeThemeJanet(theme);

    REQUIRE(janet.find(R"((ned/theme-gradient "brand" "diag #40c080 2 #2050c0"))") != std::string::npos);
    REQUIRE(janet.find(R"((ned/theme-surface "popup" "fill" "y #1e1e2ec0 3 #1e1e2e80"))") != std::string::npos);

    SECTION("a part the theme never set is not written") {
        REQUIRE(janet.find(R"((ned/theme-surface "popup" "text")") == std::string::npos);
    }

    SECTION("a stacked paint says so instead of emitting a call that would lie") {
        ned::ui::ClearSurfaceOverrides();
        Surface stacked;
        stacked.fill = ned::ui::StackPaint({*Parse("#101010"), *Parse("#20202080")});
        ned::ui::SetSurfaceOverride("panel", stacked);

        const std::string withStack = ned::ui::SerializeThemeJanet(theme);
        REQUIRE(withStack.find("# panel.fill: a stacked paint") != std::string::npos);
        REQUIRE(withStack.find(R"((ned/theme-surface "panel" "fill")") == std::string::npos);
    }
}

TEST_CASE("$desktop-accent resolves to the machine's accent, not the theme's", "[PaintParse]") {
    const PaintRegistryGuard guard;
    const Theme              theme = DarkTheme();

    // A fact about the machine, kept outside the theme so it survives a
    // theme switch -- which is the whole reason it is not just $accent.
    struct AccentGuard {
        ~AccentGuard() {
            ned::ui::SetDetectedAccent(std::nullopt);
        }
    } accentGuard;

    SECTION("unset falls back to the theme's own accent, so the slot always resolves") {
        ned::ui::SetDetectedAccent(std::nullopt);
        const auto colour = ParseColorStop("$desktop-accent", ned::ui::PaintContextFor(theme));
        REQUIRE(colour.has_value());
        REQUIRE(*colour == theme.borderAccent.foreground);
    }

    SECTION("set wins over the active theme's accent") {
        ned::ui::SetDetectedAccent(Color::RGB(0xb218b2));
        const auto colour = ParseColorStop("$desktop-accent", ned::ui::PaintContextFor(theme));
        REQUIRE(colour.has_value());
        REQUIRE(*colour == Color::RGB(0xb218b2));
        REQUIRE_FALSE(*colour == theme.borderAccent.foreground);
    }

    SECTION("it takes the same adjustments every other slot does") {
        ned::ui::SetDetectedAccent(Color::RGB(0xb218b2));
        const auto faded = ParseColorStop("$desktop-accent/50", ned::ui::PaintContextFor(theme));
        REQUIRE(faded.has_value());
        REQUIRE(static_cast<int>(faded->alpha) == 127); // 50% of 255 rounds down
    }
}
