#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

#include "Editor/Project/Replace.h"
#include "Editor/RegexPattern.h"

using ned::editor::ProjectReplace;
using ned::editor::RegexPatternError;
using ned::editor::SearchMatch;
using ned::editor::SearchPatternError;

namespace {

void Type(ProjectReplace& pr, std::string_view text) {
    for (const char c : text) {
        pr.AppendChar(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace

// project-replace-review follow-up: this class is the stage machine and the
// match preview, nothing more -- applying a replacement moved out entirely
// (BufferView::BuildProjectReplaceReview turns Matches()/PatternText()/
// ReplacementText() into an editable review multibuffer, and
// multibuffer-apply-changes applies it). The rewrite tests that used to live
// here moved with it: see BufferViewProjectReplaceTest.cpp for the regex
// half and MultibufferTest.cpp for the write half.

TEST_CASE("Full flow: pattern then replacement, leaving the matches previewed for review", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_flow";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "cat sat\n";
    }
    {
        std::ofstream(dir / "b.txt") << "the cat mat\n";
    }

    ProjectReplace pr(dir);
    REQUIRE(pr.CurrentStage() == ProjectReplace::Stage::EnteringPattern);

    Type(pr, "cat");
    pr.ConfirmPattern();
    REQUIRE(pr.CurrentStage() == ProjectReplace::Stage::EnteringReplacement);
    REQUIRE(pr.Matches().size() == 2);

    Type(pr, "dog");
    pr.ConfirmReplacement();
    REQUIRE(pr.CurrentStage() == ProjectReplace::Stage::Confirming);
    REQUIRE(pr.PatternText() == "cat");
    REQUIRE(pr.ReplacementText() == "dog");

    // Nothing has been written -- this class never touches a file at all now.
    REQUIRE(ReadFile(dir / "a.txt") == "cat sat\n");
    REQUIRE(ReadFile(dir / "b.txt") == "the cat mat\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("ConfirmPattern throws SearchPatternError and stays in EnteringPattern on invalid syntax", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_badregex";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    ProjectReplace pr(dir);
    Type(pr, "(");

    REQUIRE_THROWS_AS(pr.ConfirmPattern(), SearchPatternError);
    REQUIRE(pr.CurrentStage() == ProjectReplace::Stage::EnteringPattern);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ConfirmReplacement goes straight to Done when there are no matches", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_nomatch";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "nothing relevant\n";
    }

    ProjectReplace pr(dir);
    Type(pr, "needle");
    pr.ConfirmPattern();
    REQUIRE(pr.Matches().empty());

    Type(pr, "replacement");
    pr.ConfirmReplacement();

    REQUIRE(pr.CurrentStage() == ProjectReplace::Stage::Done);
    REQUIRE(pr.StatusText().find("No matches") == 0);

    REQUIRE(ReadFile(dir / "file.txt") == "nothing relevant\n"); // untouched

    std::filesystem::remove_all(dir);
}

TEST_CASE("Cancel at any stage ends the session without touching any file", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_cancel";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "needle here\n";
    }

    ProjectReplace pr(dir);
    Type(pr, "needle");
    pr.ConfirmPattern();
    Type(pr, "replacement");
    pr.ConfirmReplacement();
    REQUIRE(pr.CurrentStage() == ProjectReplace::Stage::Confirming);

    pr.Cancel();
    REQUIRE(pr.CurrentStage() == ProjectReplace::Stage::Done);
    REQUIRE(ReadFile(dir / "file.txt") == "needle here\n");

    std::filesystem::remove_all(dir);
}
