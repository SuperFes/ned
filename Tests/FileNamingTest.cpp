#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include "Editor/FileNaming.h"
#include "Editor/FormatRules.h"
#include "Editor/Project/Root.h"

using ned::editor::AutoHeaderGuardEnabled;
using ned::editor::CaseConvention;
using ned::editor::ExpandHeaderGuardTemplate;
using ned::editor::FileNamingRuleFor;
using ned::editor::ProjectRoot;
using ned::editor::SetAutoHeaderGuard;
using ned::editor::SetFileNamingCaseConvention;
using ned::editor::SetHeaderGuardTemplate;
using ned::editor::SetProjectRoot;

namespace {

// Every override set by these tests is cleared afterward -- process-wide
// state, same "guaranteed reset" precedent FormatRulesTest.cpp's own
// FormatRulesGuard establishes.
struct FileNamingGuard {
    std::filesystem::path savedRoot = ProjectRoot();

    ~FileNamingGuard() {
        SetFileNamingCaseConvention("file-naming-test", std::nullopt);
        SetHeaderGuardTemplate("cpp", std::nullopt);
        SetHeaderGuardTemplate("c", std::nullopt);
        SetHeaderGuardTemplate("file-naming-test", std::nullopt);
        SetAutoHeaderGuard(false);
        SetProjectRoot(savedRoot);
    }
};

} // namespace

TEST_CASE("A language with no case-convention override has one unset", "[FileNaming]") {
    const FileNamingGuard guard;
    REQUIRE_FALSE(FileNamingRuleFor("file-naming-test").caseConvention.has_value());
}

TEST_CASE("Setting and clearing a file-naming case convention round-trips", "[FileNaming]") {
    const FileNamingGuard guard;
    SetFileNamingCaseConvention("file-naming-test", CaseConvention::SnakeCase);
    REQUIRE(FileNamingRuleFor("file-naming-test").caseConvention == CaseConvention::SnakeCase);

    SetFileNamingCaseConvention("file-naming-test", std::nullopt);
    REQUIRE_FALSE(FileNamingRuleFor("file-naming-test").caseConvention.has_value());
}

TEST_CASE("cpp and c get a built-in header-guard template with nothing configured", "[FileNaming]") {
    const FileNamingGuard guard;
    const auto            cpp = FileNamingRuleFor("cpp");
    REQUIRE(cpp.headerGuardTemplate.has_value());
    REQUIRE(*cpp.headerGuardTemplate == "${PROJECT_NAME}_${FILE_NAME}_${EXT}");

    const auto c = FileNamingRuleFor("c");
    REQUIRE(c.headerGuardTemplate.has_value());
}

TEST_CASE("A language with no built-in default and nothing configured has no header-guard template",
         "[FileNaming]") {
    const FileNamingGuard guard;
    REQUIRE_FALSE(FileNamingRuleFor("file-naming-test").headerGuardTemplate.has_value());
}

TEST_CASE("An explicit header-guard template overrides the built-in default", "[FileNaming]") {
    const FileNamingGuard guard;
    SetHeaderGuardTemplate("cpp", "CUSTOM_${FILE_NAME}");
    REQUIRE(*FileNamingRuleFor("cpp").headerGuardTemplate == "CUSTOM_${FILE_NAME}");
}

TEST_CASE("An explicit empty header-guard template turns the built-in default off", "[FileNaming]") {
    const FileNamingGuard guard;
    SetHeaderGuardTemplate("cpp", std::string());
    const auto rule = FileNamingRuleFor("cpp");
    REQUIRE(rule.headerGuardTemplate.has_value());
    REQUIRE(rule.headerGuardTemplate->empty());
}

TEST_CASE("ExpandHeaderGuardTemplate substitutes PROJECT_NAME/FILE_NAME/EXT, sanitized and uppercased",
         "[FileNaming]") {
    const FileNamingGuard guard;
    SetProjectRoot("/home/user/my-project");

    const std::string result =
        ExpandHeaderGuardTemplate("${PROJECT_NAME}_${FILE_NAME}_${EXT}", "/home/user/my-project/include/widget.hpp");
    REQUIRE(result == "MY_PROJECT_WIDGET_HPP");
}

TEST_CASE("ExpandHeaderGuardTemplate collapses non-alnum runs in the file stem, not just the project name",
         "[FileNaming]") {
    const FileNamingGuard guard;
    SetProjectRoot("/home/user/proj");

    const std::string result = ExpandHeaderGuardTemplate("${FILE_NAME}", "/home/user/proj/my.weird file.h");
    REQUIRE(result == "MY_WEIRD_FILE");
}

TEST_CASE("ExpandHeaderGuardTemplate leaves a template with no recognized placeholder unchanged", "[FileNaming]") {
    const FileNamingGuard guard;
    REQUIRE(ExpandHeaderGuardTemplate("FIXED_GUARD", "/home/user/proj/widget.h") == "FIXED_GUARD");
}

TEST_CASE("AutoHeaderGuard defaults off and round-trips", "[FileNaming]") {
    const FileNamingGuard guard;
    REQUIRE_FALSE(AutoHeaderGuardEnabled());
    SetAutoHeaderGuard(true);
    REQUIRE(AutoHeaderGuardEnabled());
}
