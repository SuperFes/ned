#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

#include "Editor/Project/Replace.h"
#include "Editor/RegexPattern.h"

using ned::editor::ProjectReplace;
using ned::editor::RegexPatternError;
using ned::editor::ReplaceMatches;
using ned::editor::ReplaceSummary;
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

TEST_CASE("Full flow: pattern, replacement, confirm rewrites the matched files", "[ProjectReplace]") {
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
    REQUIRE(pr.StatusText().find("Replace matches on 2 line") == 0);
    REQUIRE(pr.StatusText().find("across 2 file") != std::string::npos);

    const ReplaceSummary summary = pr.Confirm();
    REQUIRE(pr.CurrentStage() == ProjectReplace::Stage::Done);
    REQUIRE(summary.filesChanged == 2);
    REQUIRE(summary.replacementCount == 2);

    REQUIRE(ReadFile(dir / "a.txt") == "dog sat\n");
    REQUIRE(ReadFile(dir / "b.txt") == "the dog mat\n");

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

TEST_CASE("Confirm on a fresh/Done ProjectReplace is a safe no-op returning an empty summary", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_earlyconfirm";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    ProjectReplace       pr(dir);
    const ReplaceSummary summary = pr.Confirm(); // still EnteringPattern -- not Confirming

    REQUIRE(summary.filesChanged == 0);
    REQUIRE(summary.replacementCount == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ReplaceMatches counts every occurrence, not just one per matched line", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_multiocc";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "cat cat cat\n";
    }

    const std::vector<SearchMatch> matches{SearchMatch{dir / "file.txt", 1, "cat cat cat"}};
    const ReplaceSummary           summary = ReplaceMatches(matches, "cat", "dog");

    REQUIRE(summary.filesChanged == 1);
    REQUIRE(summary.replacementCount == 3);
    REQUIRE(ReadFile(dir / "file.txt") == "dog dog dog\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("ReplaceMatches deduplicates multiple matches referencing the same file", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_dedupe";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "needle\nneedle\n";
    }

    const std::vector<SearchMatch> matches{
        SearchMatch{dir / "file.txt", 1, "needle"},
        SearchMatch{dir / "file.txt", 2, "needle"},
    };
    const ReplaceSummary summary = ReplaceMatches(matches, "needle", "found");

    REQUIRE(summary.filesChanged == 1); // one file, rewritten exactly once
    REQUIRE(summary.replacementCount == 2);
    REQUIRE(ReadFile(dir / "file.txt") == "found\nfound\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("ReplaceMatches rewrites multi-byte UTF-8 content without corrupting surrounding codepoints",
          "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_utf8";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        // "café" and "中文" bracket the ASCII match on both sides, so a
        // byte-offset-off-by-one in the rewrite would visibly corrupt one
        // of the surrounding multi-byte characters rather than just
        // mis-hitting the match itself.
        std::ofstream(dir / "file.txt") << "caf\xc3\xa9 cat \xe4\xb8\xad\xe6\x96\x87\n";
    }

    const std::vector<SearchMatch> matches{SearchMatch{dir / "file.txt", 1, "caf\xc3\xa9 cat \xe4\xb8\xad\xe6\x96\x87"}};
    const ReplaceSummary           summary = ReplaceMatches(matches, "cat", "dog");

    REQUIRE(summary.filesChanged == 1);
    REQUIRE(summary.replacementCount == 1);
    REQUIRE(ReadFile(dir / "file.txt") == "caf\xc3\xa9 dog \xe4\xb8\xad\xe6\x96\x87\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("ReplaceMatches throws RegexPatternError for an invalid pattern", "[ProjectReplace]") {
    REQUIRE_THROWS_AS(ReplaceMatches({}, "(", "x"), RegexPatternError);
}

TEST_CASE("ReplaceMatches anchors ^ per line of the file, matching the per-line search preview", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_anchor";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "foo\nbarfoo\nfoo\n";
    }

    const std::vector<SearchMatch> matches{SearchMatch{dir / "file.txt", 1, "foo"}};
    const ReplaceSummary           summary = ReplaceMatches(matches, "^foo", "baz");

    // in-file-regex follow-up: the search preview matches per line, so ^foo
    // matched lines 1 and 3 -- the rewrite (over full file content, PCRE2
    // multiline) must agree, not anchor only at the file start the way the
    // old std::regex engine did.
    REQUIRE(summary.replacementCount == 2);
    REQUIRE(ReadFile(dir / "file.txt") == "baz\nbarfoo\nbaz\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("ReplaceMatches supports lookaround and named-group replacements", "[ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_pcre2";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "count = 1; amount = 2;\n";
    }

    const std::vector<SearchMatch> matches{SearchMatch{dir / "file.txt", 1, "count = 1; amount = 2;"}};
    const ReplaceSummary           summary = ReplaceMatches(matches, "(?<name>\\w+)(?= = \\d)", "my_${name}");

    REQUIRE(summary.replacementCount == 2);
    REQUIRE(ReadFile(dir / "file.txt") == "my_count = 1; my_amount = 2;\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("ReplaceMatches preserves the rewritten file's permissions and hard links", "[ProjectReplace]") {
    // file-attribute-preservation follow-up: this rewrites the user's own
    // files with the same temp-then-rename pattern Buffer::SaveToFile uses,
    // and had the same defect -- see Text/FilePreservation.h.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_replace_test_preserve";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    const std::filesystem::path script = dir / "run.sh";
    {
        std::ofstream(script) << "echo cat\n";
    }
    std::filesystem::permissions(script, std::filesystem::perms::owner_all);

    const std::filesystem::path linked = dir / "linked.txt";
    const std::filesystem::path alias  = dir / "alias.txt";
    {
        std::ofstream(linked) << "a cat here\n";
    }
    std::filesystem::create_hard_link(linked, alias);

    const std::vector<SearchMatch> matches{
        SearchMatch{script, 1, "echo cat"},
        SearchMatch{linked, 1, "a cat here"},
    };
    const ReplaceSummary summary = ReplaceMatches(matches, "cat", "dog");
    REQUIRE(summary.filesChanged == 2);

    REQUIRE(ReadFile(script) == "echo dog\n");
    REQUIRE((std::filesystem::status(script).permissions() & std::filesystem::perms::owner_exec) !=
            std::filesystem::perms::none);

    REQUIRE(ReadFile(linked) == "a dog here\n");
    REQUIRE(std::filesystem::equivalent(linked, alias));
    REQUIRE(ReadFile(alias) == "a dog here\n");
}
