#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>

#include "Editor/ProjectSearch.h"

using ned::editor::SearchDirectory;
using ned::editor::SearchMatch;

namespace {

bool AnyMatchIn(const std::vector<SearchMatch>& matches, const std::filesystem::path& file, std::size_t line) {
    return std::any_of(matches.begin(), matches.end(),
                       [&](const SearchMatch& m) { return m.file == file && m.lineNumber == line; });
}

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
