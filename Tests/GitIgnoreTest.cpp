#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/GitIgnore.h"

using ned::editor::GitIgnoreMatcher;

TEST_CASE("GitIgnoreMatcher with no .gitignore never ignores anything", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_missing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    const GitIgnoreMatcher matcher(dir);
    REQUIRE_FALSE(matcher.IsIgnored("anything.txt", false));
    REQUIRE_FALSE(matcher.IsIgnored("some/nested/dir", true));

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitIgnoreMatcher skips comments and blank lines", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_comments";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / ".gitignore") << "# a comment\n\n   \n# another\n";
    }

    const GitIgnoreMatcher matcher(dir);
    REQUIRE_FALSE(matcher.IsIgnored("anything.txt", false));

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitIgnoreMatcher matches a plain unanchored name at any depth", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_unanchored";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / ".gitignore") << "build\n";
    }

    const GitIgnoreMatcher matcher(dir);
    REQUIRE(matcher.IsIgnored("build", true));
    REQUIRE(matcher.IsIgnored("nested/build", true)); // no '/' in the pattern -- matches at any depth

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitIgnoreMatcher: a leading slash anchors the pattern to the root", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_anchored";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / ".gitignore") << "/dist\n";
    }

    const GitIgnoreMatcher matcher(dir);
    REQUIRE(matcher.IsIgnored("dist", true));
    REQUIRE_FALSE(matcher.IsIgnored("nested/dist", true)); // anchored -- only matches at the root

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitIgnoreMatcher: an interior slash also anchors the pattern (real git's own rule)", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_interior_slash";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / ".gitignore") << "src/generated\n";
    }

    const GitIgnoreMatcher matcher(dir);
    REQUIRE(matcher.IsIgnored("src/generated", true));
    REQUIRE_FALSE(matcher.IsIgnored("other/src/generated", true));

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitIgnoreMatcher: a trailing slash is directory-only, never matches a file", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_dironly";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / ".gitignore") << "build/\n";
    }

    const GitIgnoreMatcher matcher(dir);
    REQUIRE(matcher.IsIgnored("build", true));
    REQUIRE_FALSE(matcher.IsIgnored("build", false)); // a file named "build", not a directory -- not matched

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitIgnoreMatcher: '*' matches within one path segment only", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_star";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / ".gitignore") << "*.o\n";
    }

    const GitIgnoreMatcher matcher(dir);
    REQUIRE(matcher.IsIgnored("foo.o", false));
    REQUIRE(matcher.IsIgnored("nested/foo.o", false)); // unanchored -- any depth
    REQUIRE_FALSE(matcher.IsIgnored("foo.o.txt", false));

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitIgnoreMatcher: a later negation re-includes an earlier match", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_negation";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / ".gitignore") << "*.log\n!important.log\n";
    }

    const GitIgnoreMatcher matcher(dir);
    REQUIRE(matcher.IsIgnored("debug.log", false));
    REQUIRE_FALSE(matcher.IsIgnored("important.log", false));

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitIgnoreMatcher: a later plain rule overrides an earlier negation", "[GitIgnore]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_gitignore_test_reorder";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / ".gitignore") << "!kept.log\n*.log\n";
    }

    const GitIgnoreMatcher matcher(dir);
    REQUIRE(matcher.IsIgnored("kept.log", false)); // the later "*.log" rule wins

    std::filesystem::remove_all(dir);
}
