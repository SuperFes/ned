#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "Editor/FormatConfigParse.h"
#include "Editor/FormatRules.h"
#include "Editor/IndentStyle.h"
#include "Editor/MaxConsecutiveBlankLines.h"
#include "Editor/TrimOnSave.h"
#include "Editor/FinalNewline.h"

using ned::editor::ApplyFormatConfig;
using ned::editor::BlankRuleFor;
using ned::editor::BracePlacement;
using ned::editor::BreakRuleFor;
using ned::editor::CaseConvention;
using ned::editor::CaseRuleFor;
using ned::editor::SetCaseConvention;
using ned::editor::EffectiveIndentStyle;
using ned::editor::EnsureFinalNewline;
using ned::editor::FormatConfig;
using ned::editor::IndentStyle;
using ned::editor::LoadFormatConfigFile;
using ned::editor::MaxConsecutiveBlankLines;
using ned::editor::ParseFormatConfig;
using ned::editor::PersonalFormatConfigPath;
using ned::editor::ProjectFormatConfigPath;
using ned::editor::SetBlankMaxBefore;
using ned::editor::SetBlankMinBefore;
using ned::editor::SetEnsureFinalNewline;
using ned::editor::SetIndentStyle;
using ned::editor::SetIndentStyleForMode;
using ned::editor::SetMaxConsecutiveBlankLines;
using ned::editor::SetTrimTrailingWhitespaceOnSave;
using ned::editor::SetWrapForceTrailingComma;
using ned::editor::SetWrapPolicy;
using ned::editor::SpaceRuleFor;
using ned::editor::TrimTrailingWhitespaceOnSave;
using ned::editor::WrapPolicy;
using ned::editor::WrapRuleFor;

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

// FormatRules.h is process-wide state too -- clears whatever this file's
// :space/:break tests touch.
struct FormatRulesGuard {
    ~FormatRulesGuard() {
        using namespace ned::editor;
        SetSpaceBefore("format-config-test.capture", std::nullopt);
        SetSpaceAfter("format-config-test.capture", std::nullopt);
        SetSpaceWithin("format-config-test.capture", std::nullopt);
        SetBreakBefore("format-config-test.capture", std::nullopt);
        SetBracePlacement("format-config-test.capture", std::nullopt);
        SetBraceCollapseEmpty("format-config-test.capture", std::nullopt);
        SetBraceCollapseSimple("format-config-test.capture", std::nullopt);
        SetBlankMinBefore("format-config-test.capture", std::nullopt);
        SetBlankMaxBefore("format-config-test.capture", std::nullopt);
        SetWrapPolicy("format-config-test.capture", std::nullopt);
        SetWrapForceTrailingComma("format-config-test.capture", std::nullopt);
        SetCaseConvention("format-config-test.entity", std::nullopt);
    }
};

} // namespace

TEST_CASE("ParseFormatConfig reads every field", "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig(
        "{:indent {:python {:tabs false :width 4}\n"
        "          :go     {:tabs true}}\n"
        " :trim-trailing-whitespace true\n"
        " :ensure-final-newline false\n"
        " :max-consecutive-blank-lines 3}",
        "test.janet");

    REQUIRE(config.indent.size() == 2);
    REQUIRE(config.indent.at("python").useTabs == false);
    REQUIRE(config.indent.at("python").width == 4);
    REQUIRE(config.indent.at("go").useTabs == true);
    REQUIRE_FALSE(config.indent.at("go").width.has_value());
    REQUIRE(config.trimTrailingWhitespaceOnSave == true);
    REQUIRE(config.ensureFinalNewline == false);
    REQUIRE(config.maxConsecutiveBlankLines == 3);
}

TEST_CASE("ParseFormatConfig reads :space and :break entries", "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig(
        "{:space {\"control.parens\" {:before true :after false}\n"
        "         \"cpp/control.parens\" {:within true}}\n"
        " :break {\"brace.function\" {:placement :next-line :collapse-empty true}}}",
        "test.janet");

    REQUIRE(config.space.size() == 2);
    REQUIRE(config.space.at("control.parens").before == true);
    REQUIRE(config.space.at("control.parens").after == false);
    REQUIRE_FALSE(config.space.at("control.parens").within.has_value());
    REQUIRE(config.space.at("cpp/control.parens").within == true);

    REQUIRE(config.breakRules.size() == 1);
    REQUIRE(config.breakRules.at("brace.function").placement == BracePlacement::NextLine);
    REQUIRE(config.breakRules.at("brace.function").collapseEmpty == true);
    REQUIRE_FALSE(config.breakRules.at("brace.function").before.has_value());
}

TEST_CASE("ParseFormatConfig reads :blank entries", "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig(
        "{:blank {\"def.toplevel\" {:min-before 2 :max-before 2}\n"
        "         \"def.method\" {:min-before 1}}}",
        "test.janet");

    REQUIRE(config.blank.size() == 2);
    REQUIRE(config.blank.at("def.toplevel").minBefore == 2);
    REQUIRE(config.blank.at("def.toplevel").maxBefore == 2);
    REQUIRE(config.blank.at("def.method").minBefore == 1);
    REQUIRE_FALSE(config.blank.at("def.method").maxBefore.has_value());
}

TEST_CASE("ParseFormatConfig rejects a malformed :blank shape", "[FormatConfigParse]") {
    REQUIRE_THROWS_AS(ParseFormatConfig("{:blank \"not a struct\"}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:blank {:not-a-string true}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:blank {\"x\" \"not a struct\"}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:blank {\"x\" {:unknown-field true}}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:blank {\"x\" {:min-before true}}}", "test.janet"), std::runtime_error); // wants an int
}

TEST_CASE("ParseFormatConfig reads :wrap entries", "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig(
        "{:wrap {\"wrap.args\" {:policy :always :force-trailing-comma true}\n"
        "        \"wrap.params\" {:policy :never}}}",
        "test.janet");

    REQUIRE(config.wrap.size() == 2);
    REQUIRE(config.wrap.at("wrap.args").policy == WrapPolicy::Always);
    REQUIRE(config.wrap.at("wrap.args").forceTrailingComma == true);
    REQUIRE(config.wrap.at("wrap.params").policy == WrapPolicy::Never);
    REQUIRE_FALSE(config.wrap.at("wrap.params").forceTrailingComma.has_value());
}

TEST_CASE("ParseFormatConfig rejects a malformed :wrap shape", "[FormatConfigParse]") {
    REQUIRE_THROWS_AS(ParseFormatConfig("{:wrap \"not a struct\"}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:wrap {:not-a-string true}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:wrap {\"x\" \"not a struct\"}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:wrap {\"x\" {:unknown-field true}}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:wrap {\"x\" {:policy \"not-a-keyword\"}}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:wrap {\"x\" {:policy :not-a-real-policy}}}", "test.janet"), std::runtime_error);
}

TEST_CASE("ApplyFormatConfig sets only the :wrap fields a config touches", "[FormatConfigParse]") {
    const FormatRulesGuard guard;

    FormatConfig config;
    config.wrap["format-config-test.capture"] = {.policy = WrapPolicy::Always};
    ApplyFormatConfig(config);

    REQUIRE(WrapRuleFor("format-config-test.capture").policy == WrapPolicy::Always);
    REQUIRE_FALSE(WrapRuleFor("format-config-test.capture").forceTrailingComma.has_value());
}

TEST_CASE("ParseFormatConfig reads :case entries", "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig(
        "{:case {\"function\" :camel-case\n"
        "        \"cpp/type\" :pascal-case}}",
        "test.janet");

    REQUIRE(config.caseRules.size() == 2);
    REQUIRE(config.caseRules.at("function").convention == CaseConvention::CamelCase);
    REQUIRE(config.caseRules.at("cpp/type").convention == CaseConvention::PascalCase);
}

TEST_CASE("ParseFormatConfig rejects a malformed :case shape", "[FormatConfigParse]") {
    REQUIRE_THROWS_AS(ParseFormatConfig("{:case \"not a struct\"}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:case {:not-a-string :camel-case}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:case {\"function\" \"not-a-keyword\"}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:case {\"function\" :not-a-real-convention}}", "test.janet"), std::runtime_error);
}

TEST_CASE("ApplyFormatConfig sets only the :case fields a config touches", "[FormatConfigParse]") {
    const FormatRulesGuard guard;

    FormatConfig config;
    config.caseRules["format-config-test.entity"] = {.convention = CaseConvention::SnakeCase};
    ApplyFormatConfig(config);

    REQUIRE(CaseRuleFor("format-config-test.entity").convention == CaseConvention::SnakeCase);
}

TEST_CASE("ParseFormatConfig rejects a malformed :space/:break shape", "[FormatConfigParse]") {
    REQUIRE_THROWS_AS(ParseFormatConfig("{:space \"not a struct\"}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:space {:not-a-string true}}", "test.janet"), std::runtime_error); // keys are strings
    REQUIRE_THROWS_AS(ParseFormatConfig("{:space {\"x\" \"not a struct\"}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:space {\"x\" {:unknown-field true}}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:space {\"x\" {:before 4}}}", "test.janet"), std::runtime_error); // :before wants a bool
    REQUIRE_THROWS_AS(ParseFormatConfig("{:break \"not a struct\"}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:break {\"x\" {:unknown-field true}}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:break {\"x\" {:placement \"not-a-keyword\"}}}", "test.janet"), std::runtime_error);
    REQUIRE_THROWS_AS(ParseFormatConfig("{:break {\"x\" {:placement :not-a-real-placement}}}", "test.janet"), std::runtime_error);
}

TEST_CASE("ApplyFormatConfig sets only the :space/:break fields a config touches", "[FormatConfigParse]") {
    const FormatRulesGuard guard;

    FormatConfig config;
    config.space["format-config-test.capture"] = {.before = true};
    config.breakRules["format-config-test.capture"] = {.placement = BracePlacement::SameLine};
    config.blank["format-config-test.capture"] = {.minBefore = 2};
    ApplyFormatConfig(config);

    REQUIRE(SpaceRuleFor("format-config-test.capture").before == true);
    REQUIRE_FALSE(SpaceRuleFor("format-config-test.capture").after.has_value());
    REQUIRE(BreakRuleFor("format-config-test.capture").placement == BracePlacement::SameLine);
    REQUIRE_FALSE(BreakRuleFor("format-config-test.capture").before.has_value());
    REQUIRE(BlankRuleFor("format-config-test.capture").minBefore == 2);
    REQUIRE_FALSE(BlankRuleFor("format-config-test.capture").maxBefore.has_value());
}

TEST_CASE("ParseFormatConfig accepts a negative :max-consecutive-blank-lines (the disabled sentinel)",
          "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig("{:max-consecutive-blank-lines -1}", "test.janet");
    REQUIRE(config.maxConsecutiveBlankLines == -1);
}

TEST_CASE("ParseFormatConfig leaves every field unset for an empty struct", "[FormatConfigParse]") {
    const FormatConfig config = ParseFormatConfig("{}", "test.janet");
    REQUIRE(config.indent.empty());
    REQUIRE(config.space.empty());
    REQUIRE(config.breakRules.empty());
    REQUIRE(config.blank.empty());
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
    REQUIRE_THROWS_AS(ParseFormatConfig("{:max-consecutive-blank-lines true}", "test.janet"), std::runtime_error);
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

TEST_CASE("ApplyFormatConfig applies :max-consecutive-blank-lines", "[FormatConfigParse]") {
    struct Guard {
        ~Guard() {
            SetMaxConsecutiveBlankLines(2);
        }
    } guard;

    FormatConfig config;
    config.maxConsecutiveBlankLines = 5;
    ApplyFormatConfig(config);
    REQUIRE(MaxConsecutiveBlankLines() == 5);
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
