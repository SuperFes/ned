#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "UI/ThemeFile.h"

using ned::ui::Color;
using ned::ui::DarkTheme;
using ned::ui::LightTheme;
using ned::ui::LoadThemeFile;
using ned::ui::ParseTheme;
using ned::ui::SaveThemeFile;
using ned::ui::SerializeTheme;
using ned::ui::Theme;
using ned::ui::ThemeFilePath;

namespace {

// Mirrors Tests/InitFileTest.cpp's EnvVarGuard exactly, since ThemeFilePath
// follows the same XDG resolution InitFilePath does.
class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            hadPrevious_ = true;
            previous_    = existing;
        }
        if (value) {
            setenv(name, value, 1);
        }
        else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard() {
        if (hadPrevious_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else {
            unsetenv(name_.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&)            = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

  private:
    std::string name_;
    bool        hadPrevious_ = false;
    std::string previous_;
};

} // namespace

TEST_CASE("ThemeFilePath prefers XDG_CONFIG_HOME when set", "[ThemeFile]") {
    EnvVarGuard xdg("XDG_CONFIG_HOME", "/tmp/ned-xdg-test-config");
    EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");

    REQUIRE(ThemeFilePath() == std::filesystem::path("/tmp/ned-xdg-test-config/ned/theme.txt"));
}

TEST_CASE("ThemeFilePath falls back to HOME/.config when XDG_CONFIG_HOME is unset", "[ThemeFile]") {
    EnvVarGuard xdg("XDG_CONFIG_HOME", nullptr);
    EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");

    REQUIRE(ThemeFilePath() == std::filesystem::path("/tmp/ned-xdg-test-home/.config/ned/theme.txt"));
}

TEST_CASE("ThemeFilePath throws when neither XDG_CONFIG_HOME nor HOME is set", "[ThemeFile]") {
    EnvVarGuard xdg("XDG_CONFIG_HOME", nullptr);
    EnvVarGuard home("HOME", nullptr);

    REQUIRE_THROWS_AS(ThemeFilePath(), std::runtime_error);
}

TEST_CASE("SerializeTheme/ParseTheme round-trips DarkTheme exactly", "[ThemeFile]") {
    const Theme original = DarkTheme();
    const Theme restored = ParseTheme(SerializeTheme(original), LightTheme()); // deliberately mismatched base

    REQUIRE(restored.background == original.background);
    REQUIRE(restored.defaultForeground == original.defaultForeground);
    REQUIRE(restored.commentForeground == original.commentForeground);
    REQUIRE(restored.stringForeground == original.stringForeground);
    REQUIRE(restored.keywordForeground == original.keywordForeground);
    REQUIRE(restored.numberForeground == original.numberForeground);
    REQUIRE(restored.modeLineForeground == original.modeLineForeground);
    REQUIRE(restored.modeLineGradientStart == original.modeLineGradientStart);
    REQUIRE(restored.modeLineGradientEnd == original.modeLineGradientEnd);
    REQUIRE(restored.echoArea == original.echoArea);
    REQUIRE(restored.selectionBackground == original.selectionBackground);
    REQUIRE(restored.isearchMatchBackground == original.isearchMatchBackground);
    REQUIRE(restored.tabBar.background == original.tabBar.background);
    REQUIRE(restored.tabBar.foreground == original.tabBar.foreground);
    REQUIRE(restored.activeTab.background == original.activeTab.background);
    REQUIRE(restored.activeTab.foreground == original.activeTab.foreground);
    REQUIRE(restored.scrollBar.background == original.scrollBar.background);
    REQUIRE(restored.scrollBar.foreground == original.scrollBar.foreground);
    REQUIRE(restored.scrollBarDisabled.background == original.scrollBarDisabled.background);
    REQUIRE(restored.scrollBarDisabled.foreground == original.scrollBarDisabled.foreground);
    REQUIRE(restored.binaryForeground == original.binaryForeground);
    REQUIRE(restored.linkForeground == original.linkForeground);
    REQUIRE(restored.truncationIndicatorForeground == original.truncationIndicatorForeground);
    REQUIRE(restored.unsavedChangeIndicator == original.unsavedChangeIndicator);
    REQUIRE(restored.headlineLevel1Foreground == original.headlineLevel1Foreground);
    REQUIRE(restored.headlineLevel2Foreground == original.headlineLevel2Foreground);
    REQUIRE(restored.headlineLevel3Foreground == original.headlineLevel3Foreground);
    REQUIRE(restored.todoKeywordForeground == original.todoKeywordForeground);
    REQUIRE(restored.doneKeywordForeground == original.doneKeywordForeground);
    REQUIRE(restored.checkboxForeground == original.checkboxForeground);
    REQUIRE(restored.underlineForeground == original.underlineForeground);
    REQUIRE(restored.strikethroughForeground == original.strikethroughForeground);
    REQUIRE(restored.keywordModifierForeground == original.keywordModifierForeground);
    REQUIRE(restored.methodForeground == original.methodForeground);
    REQUIRE(restored.constructorForeground == original.constructorForeground);
    REQUIRE(restored.labelForeground == original.labelForeground);
    REQUIRE(restored.returnTypeForeground == original.returnTypeForeground);
    REQUIRE(restored.includePathForeground == original.includePathForeground);
    REQUIRE(restored.border.background == original.border.background);
    REQUIRE(restored.border.foreground == original.border.foreground);
    REQUIRE(restored.borderAccent.background == original.borderAccent.background);
    REQUIRE(restored.borderAccent.foreground == original.borderAccent.foreground);
    REQUIRE(restored.modeLineFocusedGradientStart == original.modeLineFocusedGradientStart);
    REQUIRE(restored.modeLineFocusedGradientEnd == original.modeLineFocusedGradientEnd);
}

TEST_CASE("SerializeTheme/ParseTheme round-trips LightTheme's TrueColor palette exactly", "[ThemeFile]") {
    const Theme original = LightTheme();
    const Theme restored = ParseTheme(SerializeTheme(original), DarkTheme());

    REQUIRE(restored.background == original.background);
    REQUIRE(restored.defaultForeground == original.defaultForeground);
    REQUIRE(restored.selectionBackground == original.selectionBackground);
}

TEST_CASE("ParseTheme keeps base's value for a malformed or unrecognized entry", "[ThemeFile]") {
    const Theme base = DarkTheme();

    const Theme result = ParseTheme(
        "background=not-a-color\n"
        "some_future_key=irrelevant\n"
        "string_foreground=#00ff00\n",
        base);

    REQUIRE(result.background == base.background);            // malformed -- kept base's value
    REQUIRE(result.stringForeground == Color::RGB(0x00ff00)); // valid -- overridden
}

TEST_CASE("ParseTheme understands the \"default\" sentinel as a pass-through Color", "[ThemeFile]") {
    const Theme result = ParseTheme("background=default\n", LightTheme()); // LightTheme's own background is opaque RGB

    REQUIRE(result.background == Color::Default);
}

TEST_CASE("SaveThemeFile/LoadThemeFile round-trip through a real file", "[ThemeFile]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_theme_file_test" / "theme.txt";
    std::filesystem::remove_all(path.parent_path());

    SaveThemeFile(LightTheme(), path);
    REQUIRE(std::filesystem::exists(path));

    const auto loaded = LoadThemeFile(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->background == LightTheme().background);

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("LoadThemeFile returns nullopt for a missing file", "[ThemeFile]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_theme_file_test_missing.txt";
    std::filesystem::remove(path);

    REQUIRE_FALSE(LoadThemeFile(path).has_value());
}

// theme-editing follow-up: the shared key table's new coverage and the
// theme.janet side.

TEST_CASE("Round-trip preserves the per-SyntaxClass colors the old serializer dropped", "[ThemeFile]") {
    // These 19 fields were never serialized before the shared key table --
    // fine for --detect-theme's chrome-focused output, fatal for
    // save-theme's edit-and-reload workflow.
    const Theme original = DarkTheme();
    const Theme restored = ParseTheme(SerializeTheme(original), LightTheme()); // deliberately mismatched base

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

TEST_CASE("Palette-index gradient endpoints round-trip since the hex-only restriction was dropped", "[ThemeFile]") {
    // The ANSI fallback themes made a Palette16 gradient endpoint genuinely
    // meaningful (equal endpoints pass through Interpolate unchanged).
    const Theme original = ned::ui::AnsiDarkTheme();
    const Theme restored = ParseTheme(SerializeTheme(original), DarkTheme());

    REQUIRE(restored.modeLineGradientStart == original.modeLineGradientStart);
    REQUIRE(restored.modeLineGradientEnd == original.modeLineGradientEnd);
}

TEST_CASE("SetThemeColorByKey assigns known keys and rejects unknown keys or bad tokens", "[ThemeFile]") {
    Theme theme = DarkTheme();

    REQUIRE(ned::ui::SetThemeColorByKey(theme, "keyword_foreground", "#f042d6"));
    REQUIRE(theme.keywordForeground == Color::RGB(0xf042d6));

    REQUIRE(ned::ui::SetThemeColorByKey(theme, "border_accent_foreground", "x:5"));
    REQUIRE(theme.borderAccent.foreground == Color::Palette(5));

    REQUIRE_FALSE(ned::ui::SetThemeColorByKey(theme, "no_such_key", "#112233"));
    REQUIRE_FALSE(ned::ui::SetThemeColorByKey(theme, "keyword_foreground", "not-a-color"));
    REQUIRE(theme.keywordForeground == Color::RGB(0xf042d6)); // the bad token assigned nothing
}

TEST_CASE("SerializeThemeJanet emits one ned/theme-set call per serialized color, round-trippable", "[ThemeFile]") {
    const Theme       original = DarkTheme();
    const std::string janet    = ned::ui::SerializeThemeJanet(original);

    REQUIRE(janet.find("(ned/theme-set \"background\" ") != std::string::npos);
    REQUIRE(janet.find("(ned/theme-set \"keyword_foreground\" ") != std::string::npos);
    REQUIRE(janet.find("(ned/theme-set \"border_accent_foreground\" ") != std::string::npos);

    // Re-apply every emitted (key, token) pair onto a mismatched base via
    // SetThemeColorByKey -- exactly what the real ned/theme-set path does at
    // startup -- and require the result to serialize identically to the
    // original: the generated Janet is a complete, lossless snapshot.
    Theme              rebuilt = LightTheme();
    std::istringstream in{janet};
    std::string        line;
    while (std::getline(in, line)) {
        if (line.rfind("(ned/theme-set \"", 0) != 0) {
            continue; // header comment
        }
        const std::size_t keyStart = std::strlen("(ned/theme-set \"");
        const std::size_t keyEnd   = line.find('"', keyStart);
        const std::size_t valStart = line.find('"', keyEnd + 1) + 1;
        const std::size_t valEnd   = line.find('"', valStart);
        REQUIRE(ned::ui::SetThemeColorByKey(rebuilt, line.substr(keyStart, keyEnd - keyStart),
                                            line.substr(valStart, valEnd - valStart)));
    }
    REQUIRE(SerializeTheme(rebuilt) == SerializeTheme(original));
}

TEST_CASE("ThemeJanetFilePath sits beside the theme.txt path", "[ThemeFile]") {
    EnvVarGuard xdg("XDG_CONFIG_HOME", "/tmp/ned-xdg-test-config");
    REQUIRE(ned::ui::ThemeJanetFilePath() == std::filesystem::path("/tmp/ned-xdg-test-config/ned/theme.janet"));
}
