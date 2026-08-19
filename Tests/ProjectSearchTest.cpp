#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#include "Editor/ProjectSearch.h"

using ned::editor::SearchDirectory;
using ned::editor::SearchMatch;

namespace {

bool AnyMatchIn(const std::vector<SearchMatch>& matches, const std::filesystem::path& file, std::size_t line) {
    return std::any_of(matches.begin(), matches.end(),
                       [&](const SearchMatch& m) { return m.file == file && m.lineNumber == line; });
}

// ripgrep-search follow-up. Mirrors BufferViewTest.cpp's own EnvVarGuard
// exactly (kept as a separate copy rather than shared -- not worth a new
// dependency between the two test binaries for something this small, the
// same call this codebase's own production code already makes elsewhere).
// Used to force SearchDirectory down its builtin-scanner fallback path by
// pointing $PATH somewhere with no "rg" on it, regardless of whether the
// machine actually running this test suite has ripgrep installed.
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

TEST_CASE("SearchDirectory finds matches across multiple files with 1-indexed line numbers", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_basic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    {
        std::ofstream(dir / "a.txt") << "first line\nneedle here\nlast line\n";
    }
    {
        std::ofstream(dir / "b.txt") << "no match\nneedle again\n";
    }

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");

    REQUIRE(matches.size() == 2);
    REQUIRE(AnyMatchIn(matches, std::filesystem::absolute(dir / "a.txt"), 2));
    REQUIRE(AnyMatchIn(matches, std::filesystem::absolute(dir / "b.txt"), 2));

    const auto found = std::find_if(matches.begin(), matches.end(),
                                    [&](const SearchMatch& m) { return m.lineNumber == 2 && m.file.filename() == "a.txt"; });
    REQUIRE(found != matches.end());
    REQUIRE(found->lineText == "needle here");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory returns absolute paths regardless of how root was given", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_absolute";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "needle\n";
    }

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().file.is_absolute());

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory skips dot-directories entirely", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_dotdir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / ".git");
    {
        std::ofstream(dir / ".git" / "hidden.txt") << "needle\n";
    }
    {
        std::ofstream(dir / "visible.txt") << "needle\n";
    }

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().file.filename() == "visible.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory skips files that look binary", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_binary";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    {
        std::ofstream binary(dir / "data.bin", std::ios::binary);
        binary << "needle" << '\0' << "more needle bytes";
    }
    {
        std::ofstream(dir / "text.txt") << "needle\n";
    }

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().file.filename() == "text.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory returns an empty list for a nonexistent root, without throwing", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_missing";
    std::filesystem::remove_all(dir);

    REQUIRE(SearchDirectory(dir, "needle").empty());
}

TEST_CASE("SearchDirectory returns an empty list when nothing matches", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_nomatch";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "nothing interesting here\n";
    }

    REQUIRE(SearchDirectory(dir, "needle").empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory throws std::regex_error for an invalid pattern", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_badregex";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    REQUIRE_THROWS_AS(SearchDirectory(dir, "("), std::regex_error);

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory skips a directory excluded by .gitignore, without descending into it", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_gitignore";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "build" / "nested");
    // A real .gitignore almost always implies a real git repo -- a bare
    // .gitignore with no .git marker at all is not itself a case either
    // backend needs to treat as "this project has a real .gitignore":
    // ripgrep's own VCS-ignore support only activates once it can actually
    // detect a repository (confirmed directly, not assumed), and this
    // fixture mirrors that real-world shape rather than the rarer one.
    std::filesystem::create_directory(dir / ".git");
    {
        std::ofstream(dir / ".gitignore") << "build/\n";
    }
    {
        std::ofstream(dir / "build" / "nested" / "hidden.txt") << "needle\n";
    }
    {
        std::ofstream(dir / "visible.txt") << "needle\n";
    }

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().file.filename() == "visible.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory honors a .gitignore glob pattern and a negation", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_gitignore_glob";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    std::filesystem::create_directory(dir / ".git"); // see the sibling .gitignore test's own comment for why
    {
        std::ofstream(dir / ".gitignore") << "*.log\n!keep.log\n";
    }
    {
        std::ofstream(dir / "debug.log") << "needle\n";
    }
    {
        std::ofstream(dir / "keep.log") << "needle\n";
    }

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().file.filename() == "keep.log");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory still returns correct results when ripgrep isn't on $PATH (builtin-scanner fallback)",
          "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_no_rg";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "first\nneedle here\n";
    }

    // An empty $PATH means FindRipgrepOnPath() can't find anything --
    // forces SearchDirectory down the builtin std::filesystem::
    // recursive_directory_iterator + std::regex_search path regardless of
    // whether ripgrep is actually installed on the machine running this
    // test suite.
    const EnvVarGuard pathGuard("PATH", "");

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().file.filename() == "a.txt");
    REQUIRE(matches.front().lineNumber == 2);
    REQUIRE(matches.front().lineText == "needle here");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory throws std::regex_error before ever attempting to run ripgrep", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_badregex_rg";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    // Whether or not ripgrep is installed and would be tried, an invalid
    // pattern must be rejected first, uniformly -- SearchDirectory
    // constructs its own std::regex before ever looking for "rg" on $PATH.
    REQUIRE_THROWS_AS(SearchDirectory(dir, "("), std::regex_error);

    std::filesystem::remove_all(dir);
}
