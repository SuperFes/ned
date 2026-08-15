#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
