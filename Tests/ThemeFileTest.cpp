#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "UI/ThemeFile.h"

using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::LightTheme;
using ned::ui::SetThemeColorByKey;
using ned::ui::Theme;
using ned::ui::ThemeKeys;
using ned::ui::ThemeValueByKey;

namespace {

// Every key applied onto `base`, taking each value from `from` -- what
// writing one theme's worth of (ned/theme-set ...) calls into an init.janet
// and starting ned does, minus the Janet.
Theme ApplyAllKeys(const Theme& from, Theme base) {
    for (const std::string& key : ThemeKeys()) {
        const std::optional<std::string> value = ThemeValueByKey(from, key);
        REQUIRE(value.has_value());
        REQUIRE(SetThemeColorByKey(base, key, *value));
    }
    return base;
}

} // namespace

TEST_CASE("Every listed key reads back and assigns", "[ThemeFile]") {
    // ThemeKeys and the ThemeValueByKey/SetThemeColorByKey pair are three
    // walks over one table; nothing but this holds them together, and a key
    // that lists but does not assign would be silently unsettable.
    const std::vector<std::string> keys = ThemeKeys();
    REQUIRE(keys.size() > 60); // the whole vocabulary, not a subset

    // No duplicates -- a repeated key would mean two fields fighting over
    // one name, with the first always winning.
    std::vector<std::string> sorted = keys;
    std::sort(sorted.begin(), sorted.end());
    REQUIRE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());

    Theme theme = DarkTheme();
    for (const std::string& key : keys) {
        INFO(key);
        const std::optional<std::string> value = ThemeValueByKey(theme, key);
        REQUIRE(value.has_value());
        REQUIRE(SetThemeColorByKey(theme, key, *value));
    }

    REQUIRE_FALSE(ThemeValueByKey(theme, "no_such_key").has_value());
}

TEST_CASE("The key table covers a whole theme, losslessly", "[ThemeFile]") {
    // Applying every key of one theme onto a deliberately mismatched base
    // has to produce the first theme exactly -- which is what makes "write
    // your theme as ned/theme-set calls in init.janet" a complete story
    // rather than one that silently drops whatever the table forgot.
    const Theme original = DarkTheme();
    const Theme rebuilt  = ApplyAllKeys(original, LightTheme());

    for (const std::string& key : ThemeKeys()) {
        INFO(key);
        REQUIRE(ThemeValueByKey(rebuilt, key) == ThemeValueByKey(original, key));
    }
}

TEST_CASE("Round-trip preserves the per-SyntaxClass colors an older table dropped", "[ThemeFile]") {
    // These 19 fields were never covered before the shared key table. Named
    // individually rather than left to the whole-theme case above, because
    // that is the regression this guards.
    const Theme original = DarkTheme();
    const Theme restored = ApplyAllKeys(original, LightTheme());

    REQUIRE(restored.docCommentForeground == original.docCommentForeground);
    REQUIRE(restored.stringEscapeForeground == original.stringEscapeForeground);
    REQUIRE(restored.controlKeywordForeground == original.controlKeywordForeground);
    REQUIRE(restored.functionForeground == original.functionForeground);
    REQUIRE(restored.functionBuiltinForeground == original.functionBuiltinForeground);
    REQUIRE(restored.typeForeground == original.typeForeground);
    REQUIRE(restored.typeBuiltinForeground == original.typeBuiltinForeground);
    REQUIRE(restored.constantForeground == original.constantForeground);
    REQUIRE(restored.constantBuiltinForeground == original.constantBuiltinForeground);
    REQUIRE(restored.variableForeground == original.variableForeground);
    REQUIRE(restored.variableBuiltinForeground == original.variableBuiltinForeground);
    REQUIRE(restored.parameterForeground == original.parameterForeground);
    REQUIRE(restored.propertyForeground == original.propertyForeground);
    REQUIRE(restored.operatorForeground == original.operatorForeground);
    REQUIRE(restored.punctuationForeground == original.punctuationForeground);
    REQUIRE(restored.tagForeground == original.tagForeground);
    REQUIRE(restored.attributeForeground == original.attributeForeground);
    REQUIRE(restored.namespaceForeground == original.namespaceForeground);
    REQUIRE(restored.markupMarkerForeground == original.markupMarkerForeground);
    REQUIRE(restored.ghostTextForeground == original.ghostTextForeground);
}

TEST_CASE("Brush traits round-trip through the key table, not just colours", "[ThemeFile]") {
    Theme original          = DarkTheme();
    original.activeTab.bold = false; // flip away from DarkTheme's own true, so a dropped trait would show

    const Theme rebuilt = ApplyAllKeys(original, LightTheme());
    REQUIRE_FALSE(rebuilt.activeTab.bold);
    REQUIRE(ThemeValueByKey(rebuilt, "active_tab_bold") == std::optional<std::string>{"false"});
}

TEST_CASE("A legacy x:<n> token still loads, as real RGB", "[ThemeFile]") {
    // An init.janet from before themes went truecolor-only can still carry
    // palette indices. They must keep resolving -- but as RGB,
    // since nothing puts a palette index back into a theme now.
    Theme theme = DarkTheme();

    REQUIRE(ned::ui::SetThemeColorByKey(theme, "border_accent_foreground", "x:5"));
    REQUIRE(theme.borderAccent.foreground.kind == Color::Kind::TrueColor);
    REQUIRE(theme.borderAccent.foreground == Color::RGB(0x800080)); // xterm's own magenta

    REQUIRE(ned::ui::SetThemeColorByKey(theme, "border_accent_foreground", "x:244"));
    REQUIRE(theme.borderAccent.foreground == Color::RGB(0x808080)); // the 232-255 grey ramp

    REQUIRE_FALSE(ned::ui::SetThemeColorByKey(theme, "border_accent_foreground", "x:999"));
}

TEST_CASE("SetThemeColorByKey assigns known keys and rejects unknown keys or bad tokens", "[ThemeFile]") {
    Theme theme = DarkTheme();

    REQUIRE(ned::ui::SetThemeColorByKey(theme, "keyword_foreground", "#f042d6"));
    REQUIRE(theme.keywordForeground == Color::RGB(0xf042d6));

    REQUIRE_FALSE(ned::ui::SetThemeColorByKey(theme, "no_such_key", "#112233"));
    REQUIRE_FALSE(ned::ui::SetThemeColorByKey(theme, "keyword_foreground", "not-a-color"));
    REQUIRE(theme.keywordForeground == Color::RGB(0xf042d6)); // the bad token assigned nothing
}

// bold/italic-round-trip follow-up: a Brush's trait fields (bold/italic/
// underlined/strikethrough), previously silently dropped by every
// serialization path here -- see this file's own header comment.

TEST_CASE("SetThemeColorByKey assigns a Brush's bold/italic/underlined/strikethrough traits via true/false tokens",
          "[ThemeFile]") {
    Theme theme = DarkTheme();

    REQUIRE(theme.activeTab.bold); // DarkTheme's own starting value
    REQUIRE(ned::ui::SetThemeColorByKey(theme, "active_tab_bold", "false"));
    REQUIRE_FALSE(theme.activeTab.bold);

    REQUIRE(ned::ui::SetThemeColorByKey(theme, "border_italic", "true"));
    REQUIRE(theme.border.italic);
    REQUIRE(ned::ui::SetThemeColorByKey(theme, "border_underlined", "true"));
    REQUIRE(theme.border.underlined);
    REQUIRE(ned::ui::SetThemeColorByKey(theme, "border_strikethrough", "true"));
    REQUIRE(theme.border.strikethrough);

    // "border" is a prefix of "border_accent" -- confirms a trait key
    // resolves to the right entry, not the shorter prefix's own leftover
    // suffix match (the same disambiguation the pre-existing background/
    // foreground suffix check already relies on).
    REQUIRE(ned::ui::SetThemeColorByKey(theme, "border_accent_bold", "false"));
    REQUIRE_FALSE(theme.borderAccent.bold);
    REQUIRE(theme.border.bold == DarkTheme().border.bold); // untouched

    REQUIRE_FALSE(ned::ui::SetThemeColorByKey(theme, "active_tab_bold", "not-a-bool"));
    REQUIRE_FALSE(theme.activeTab.bold); // the bad token assigned nothing, prior value kept
    REQUIRE_FALSE(ned::ui::SetThemeColorByKey(theme, "no_such_prefix_bold", "true"));
}
