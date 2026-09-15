#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "Editor/FormatConfigParse.h"
#include "Editor/IndentStyle.h"
#include "Editor/TrimOnSave.h"
#include "Editor/FinalNewline.h"

using ned::editor::ApplyFormatConfig;
using ned::editor::EffectiveIndentStyle;
using ned::editor::EnsureFinalNewline;
using ned::editor::FormatConfig;
using ned::editor::IndentStyle;
using ned::editor::LoadFormatConfigFile;
using ned::editor::ParseFormatConfig;
using ned::editor::PersonalFormatConfigPath;
using ned::editor::ProjectFormatConfigPath;
using ned::editor::SetEnsureFinalNewline;
using ned::editor::SetIndentStyle;
using ned::editor::SetIndentStyleForMode;
using ned::editor::SetTrimTrailingWhitespaceOnSave;
using ned::editor::TrimTrailingWhitespaceOnSave;

namespace {

// InitFileTest.cpp's own EnvVarGuard, duplicated rather than shared -- a
// small, self-contained utility, same tolerance this codebase already
// extends to WrapOverridesTest.cpp/IndentStyleTest.cpp's own per-file guard
// types.
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

// IndentStyleTest.cpp's own guard -- the process-wide default is global
// state that must be restored for the next test.
struct IndentStyleGuard {
    ~IndentStyleGuard() {
        SetIndentStyle(IndentStyle{});
    }
};

// TrimOnSaveTest.cpp/FinalNewlineTest.cpp's own guards -- both default on.
struct TrimOnSaveGuard {
    ~TrimOnSaveGuard() {
        SetTrimTrailingWhitespaceOnSave(true);
    }
};
struct FinalNewlineGuard {
    ~FinalNewlineGuard() {
        SetEnsureFinalNewline(true);
    }
};

} // namespace

TEST_CASE("ParseFormatConfig reads every field", "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig(
        "{:indent {:python {:tabs false :width 4}\n"
        "          :go     {:tabs true}}\n"
        " :trim-trailing-whitespace true\n"
        " :ensure-final-newline false}",
        "test.janet");

    REQUIRE(config.indent.size() == 2);
    REQUIRE(config.indent.at("python").useTabs == false);
    REQUIRE(config.indent.at("python").width == 4);
    REQUIRE(config.indent.at("go").useTabs == true);
    REQUIRE_FALSE(config.indent.at("go").width.has_value());
    REQUIRE(config.trimTrailingWhitespaceOnSave == true);
    REQUIRE(config.ensureFinalNewline == false);
}

TEST_CASE("ParseFormatConfig leaves every field unset for an empty struct", "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig("{}", "test.janet");
    REQUIRE(config.indent.empty());
    REQUIRE_FALSE(config.trimTrailingWhitespaceOnSave.has_value());
    REQUIRE_FALSE(config.ensureFinalNewline.has_value());
}

TEST_CASE("ParseFormatConfig rejects what it does not know or a wrong-typed value", "[FormatConfigParse]") {
    REQUIRE_THROWS_AS(ParseFormatConfig("[\"not\" \"a\" \"struct\"]", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:unknown-key true}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:indent \"not a struct\"}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:indent {:python \"not a struct\"}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:indent {:python {:unknown-field true}}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:indent {:python {:tabs 4}}}", "test.janet"), std::runtime_error); // :tabs wants a bool
    REQUIRE_THROWS_AS(ParseFormatConfig("{:indent {:python {:width true}}}", "test.janet"), std::runtime_error); // :width wants an int
    REQUIRE_THROWS_AS(ParseFormatConfig("{:indent {:python {:width 4.5}}}", "test.janet"), std::runtime_error); // no fractional widths
    REQUIRE_THROWS_AS(ParseFormatConfig("{:trim-trailing-whitespace \"yes\"}", "test.janet"), std::runtime_error);
}

TEST_CASE("ParseFormatConfig error messages carry the path and line", "[FormatConfigParse]") {
    try {
        (void)ParseFormatConfig("{:indent\n {:python {:unknown-field true}}}", "some/path/format.janet");
        FAIL("expected a throw");
    }
    catch (const std::runtime_error& e) {
        const std::string message = e.what();
        REQUIRE(message.starts_with("some/path/format.janet:2:")); // the :unknown-field pair is on line 2
        REQUIRE(message.find("unknown-field") != std::string::npos);
    }
}

TEST_CASE("ApplyFormatConfig sets only the fields a config touches", "[FormatConfigParse]") {
    const TrimOnSaveGuard    trimGuard;
    const FinalNewlineGuard  finalNewlineGuard;
    SetEnsureFinalNewline(true); // known starting value, so "untouched" below is meaningful

    FormatConfig config;
    config.trimTrailingWhitespaceOnSave = false;
    // ensureFinalNewline deliberately left unset.
    ApplyFormatConfig(config);

    REQUIRE_FALSE(TrimTrailingWhitespaceOnSave());
    REQUIRE(EnsureFinalNewline()); // untouched -- still the known starting value
}

TEST_CASE("ApplyFormatConfig cascades per field: a later config overrides only what it sets", "[FormatConfigParse]") {
    const IndentStyleGuard guard;

    FormatConfig personal;
    personal.indent["format-config-test-lang"] = {.useTabs = true, .width = 8};
    ApplyFormatConfig(personal);

    IndentStyle afterPersonal = EffectiveIndentStyle("format-config-test-lang-mode");
    REQUIRE(afterPersonal.useTabs);
    REQUIRE(afterPersonal.width == 8);

    // The project layer touches only :width -- :tabs must survive from the
    // personal layer, not reset to some hardcoded C++ default.
    FormatConfig project;
    project.indent["format-config-test-lang"] = {.useTabs = std::nullopt, .width = 2};
    ApplyFormatConfig(project);

    const IndentStyle afterProject = EffectiveIndentStyle("format-config-test-lang-mode");
    REQUIRE(afterProject.useTabs); // preserved from the personal layer
    REQUIRE(afterProject.width == 2); // overridden by the project layer
}

TEST_CASE("PersonalFormatConfigPath prefers XDG_CONFIG_HOME, falls back to HOME, throws with neither",
          "[FormatConfigParse]") {
    {
        EnvVarGuard xdg("XDG_CONFIG_HOME", "/tmp/ned-format-config-test-xdg");
        EnvVarGuard home("HOME", "/tmp/ned-format-config-test-home");
        REQUIRE(PersonalFormatConfigPath() == std::filesystem::path("/tmp/ned-format-config-test-xdg/ned/format.janet"));
    }
    {
        EnvVarGuard xdg("XDG_CONFIG_HOME", nullptr);
        EnvVarGuard home("HOME", "/tmp/ned-format-config-test-home");
        REQUIRE(PersonalFormatConfigPath() ==
                std::filesystem::path("/tmp/ned-format-config-test-home/.config/ned/format.janet"));
    }
    {
        EnvVarGuard xdg("XDG_CONFIG_HOME", nullptr);
        EnvVarGuard home("HOME", nullptr);
        REQUIRE_THROWS_AS(PersonalFormatConfigPath(), std::runtime_error);
    }
}

TEST_CASE("ProjectFormatConfigPath is projectRoot/.ned/format.janet", "[FormatConfigParse]") {
    REQUIRE(ProjectFormatConfigPath("/some/project") == std::filesystem::path("/some/project/.ned/format.janet"));
}

TEST_CASE("LoadFormatConfigFile is a no-op when the file doesn't exist", "[FormatConfigParse]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned_format_config_test_missing" / "format.janet";
    std::filesystem::remove_all(path.parent_path());

    LoadFormatConfigFile(path); // should not throw

    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("LoadFormatConfigFile reads, applies, and reports a schema error with the real file path",
          "[FormatConfigParse]") {
    const IndentStyleGuard guard;
    const std::filesystem::path dir  = std::filesystem::temp_directory_path() / "ned_format_config_test_present";
    const std::filesystem::path path = dir / "format.janet";
    std::filesystem::create_directories(dir);

    {
        std::ofstream out(path);
        out << "{:indent {:format-config-test-load {:width 2}}}";
    }
    LoadFormatConfigFile(path);
    REQUIRE(EffectiveIndentStyle("format-config-test-load-mode").width == 2);

    {
        std::ofstream out(path, std::ios::trunc);
        out << "{:unknown-key true}";
    }
    try {
        LoadFormatConfigFile(path);
        FAIL("expected a throw");
    }
    catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).starts_with(path.string()));
    }

    std::filesystem::remove_all(dir);
}
