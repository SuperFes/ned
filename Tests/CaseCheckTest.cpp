#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

#include "Editor/FormatRules.h"
#include "Editor/Project/CaseCheck.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::CaseConvention;
using ned::editor::CollectProjectCaseViolations;
using ned::editor::ProjectCaseViolation;
using ned::editor::SetCaseConvention;

namespace {

// case-kind follow-up: the same test-isolation lesson FormatCaseTest.cpp's
// own FormatRulesGuard already learned once.
struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetCaseConvention("parameter", std::nullopt);
        SetCaseConvention("local", std::nullopt);
        SetCaseConvention("function", std::nullopt);
        SetCaseConvention("type", std::nullopt);
        SetCaseConvention("namespace", std::nullopt);
    }
};

const ProjectCaseViolation* FindByName(const std::vector<ProjectCaseViolation>& violations, std::string_view name) {
    const auto it = std::find_if(violations.begin(), violations.end(),
                                 [&](const ProjectCaseViolation& v) { return v.violation.name == name; });
    return it == violations.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("CollectProjectCaseViolations scans every file its mode can check, skipping non-source files entirely",
          "[CaseCheck]") {
    const FormatRulesGuard      guard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_case_check_test_basic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    SetCaseConvention("function", CaseConvention::SnakeCase);
    {
        std::ofstream(dir / "a.cpp") << "void BadName() {\n}\n";
    }
    {
        std::ofstream(dir / "b.cpp") << "void good_name() {\n}\n";
    }
    // Not a source file at all -- a mode with neither localScopes nor
    // symbolKind should contribute nothing, and never even be parsed.
    {
        std::ofstream(dir / "c.txt") << "void BadNameToo() {\n}\n";
    }

    const std::vector<ProjectCaseViolation> violations = CollectProjectCaseViolations(dir);

    REQUIRE(FindByName(violations, "BadName") != nullptr);
    REQUIRE(FindByName(violations, "good_name") == nullptr);
    REQUIRE(FindByName(violations, "BadNameToo") == nullptr);

    const ProjectCaseViolation* bad = FindByName(violations, "BadName");
    REQUIRE(bad->file.filename() == "a.cpp");
    REQUIRE(bad->line == 1);
    REQUIRE(bad->violation.suggestedName == "bad_name");

    std::filesystem::remove_all(dir);
}

TEST_CASE("CollectProjectCaseViolations reports nothing when no entity kind has a convention configured",
          "[CaseCheck]") {
    const FormatRulesGuard      guard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_case_check_test_unconfigured";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.cpp") << "void Bad_Name(int Also_Bad) {\n}\n";
    }

    REQUIRE(CollectProjectCaseViolations(dir).empty());
    std::filesystem::remove_all(dir);
}

TEST_CASE("CollectProjectCaseViolations computes the 1-indexed line the name starts on", "[CaseCheck]") {
    const FormatRulesGuard      guard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_case_check_test_line";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    SetCaseConvention("function", CaseConvention::SnakeCase);
    {
        std::ofstream(dir / "a.cpp") << "// a comment\n// another\nvoid BadName() {\n}\n";
    }

    const std::vector<ProjectCaseViolation> violations = CollectProjectCaseViolations(dir);
    const ProjectCaseViolation*              bad        = FindByName(violations, "BadName");
    REQUIRE(bad != nullptr);
    REQUIRE(bad->line == 3);

    std::filesystem::remove_all(dir);
}

TEST_CASE("CollectProjectCaseViolations prefers a live, modified open buffer's content over the file on disk",
          "[CaseCheck]") {
    const FormatRulesGuard      guard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_case_check_test_live_buffer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    SetCaseConvention("function", CaseConvention::SnakeCase);
    const std::filesystem::path filePath = dir / "a.cpp";
    { std::ofstream(filePath) << "void good_name() {\n}\n"; }

    ned::text::BufferList bufferList;
    ned::text::Buffer&    buffer = bufferList.OpenFile(filePath);
    // Edit in memory without saving -- the disk copy still says good_name.
    buffer.DeleteRange(0, buffer.Content().ByteLength());
    buffer.InsertAtPoint("void BadName() {\n}\n");
    REQUIRE(buffer.Modified());

    const std::vector<ProjectCaseViolation> violations = CollectProjectCaseViolations(dir, &bufferList);
    REQUIRE(FindByName(violations, "BadName") != nullptr);
    REQUIRE(FindByName(violations, "good_name") == nullptr);

    std::filesystem::remove_all(dir);
}

