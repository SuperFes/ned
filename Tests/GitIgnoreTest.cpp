#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/GitIgnore.h"
#include "EnvOverride.h"

using ned::editor::CachedGitIgnoreMatcher;
using ned::editor::GitIgnoreMatcher;
using ned::tests::ScopedEnvOverride;

namespace {

// One disposable temp tree per test, mirroring NodeModulesTest.cpp's own
// "real filesystem, cleaned up in the destructor" convention.
struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const std::string& name) : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::filesystem::remove_all(path);
    }
};

// Pins HOME/$XDG_CONFIG_HOME/$GIT_CONFIG_GLOBAL to a disposable location --
// required for any fixture carrying a .git entry, which is what makes
// GitIgnoreMatcher consult the machine's real global git configuration at
// all (see EnvOverride.h).
struct HermeticGitEnv {
    std::filesystem::path homeDir;
    std::filesystem::path xdgDir;
    ScopedEnvOverride     home;
    ScopedEnvOverride     xdg;
    ScopedEnvOverride     gitConfigGlobal;

    explicit HermeticGitEnv(const std::filesystem::path& base) : homeDir(base / "fake-home"),
                                                                 xdgDir(base / "fake-home" / ".config"),
                                                                 home("HOME", homeDir.c_str()),
                                                                 xdg("XDG_CONFIG_HOME", xdgDir.c_str()),
                                                                 gitConfigGlobal("GIT_CONFIG_GLOBAL", nullptr) {
        std::filesystem::create_directories(xdgDir);
    }
};

} // namespace

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

TEST_CASE("GitIgnoreMatcher: a nested .gitignore's patterns are relative to its own directory", "[GitIgnore]") {
    TempDir dir("ned_gitignore_test_nested");
    std::filesystem::create_directories(dir.path / "sub" / "deep");
    {
        std::ofstream(dir.path / "sub" / ".gitignore") << "*.log\n/gen\n";
    }

    const GitIgnoreMatcher matcher(dir.path);
    CHECK(matcher.IsIgnored("sub/debug.log", false));
    CHECK(matcher.IsIgnored("sub/deep/debug.log", false)); // unanchored -- any depth below its own directory
    CHECK_FALSE(matcher.IsIgnored("debug.log", false));    // root level -- the nested file doesn't reach up
    CHECK(matcher.IsIgnored("sub/gen", true));
    CHECK_FALSE(matcher.IsIgnored("sub/deep/gen", true)); // anchored to the nested file's own directory
}

TEST_CASE("GitIgnoreMatcher: a deeper .gitignore's negation overrides the root's rule", "[GitIgnore]") {
    TempDir dir("ned_gitignore_test_nested_negation");
    std::filesystem::create_directories(dir.path / "sub");
    {
        std::ofstream(dir.path / ".gitignore") << "*.log\n";
    }
    {
        std::ofstream(dir.path / "sub" / ".gitignore") << "!keep.log\n";
    }

    const GitIgnoreMatcher matcher(dir.path);
    CHECK(matcher.IsIgnored("debug.log", false));
    CHECK(matcher.IsIgnored("sub/debug.log", false));
    CHECK_FALSE(matcher.IsIgnored("sub/keep.log", false)); // deeper file wins
    CHECK(matcher.IsIgnored("keep.log", false));           // the nested negation doesn't reach the root
}

TEST_CASE("GitIgnoreMatcher: .git/info/exclude applies, below .gitignore in precedence", "[GitIgnore]") {
    TempDir              dir("ned_gitignore_test_info_exclude");
    const HermeticGitEnv env(dir.path);
    std::filesystem::create_directories(dir.path / ".git" / "info");
    {
        std::ofstream(dir.path / ".git" / "info" / "exclude") << "*.tmp\n";
    }
    {
        std::ofstream(dir.path / ".gitignore") << "!keep.tmp\n";
    }

    const GitIgnoreMatcher matcher(dir.path);
    CHECK(matcher.IsIgnored("scratch.tmp", false));
    CHECK_FALSE(matcher.IsIgnored("keep.tmp", false)); // .gitignore outranks info/exclude
}

TEST_CASE("GitIgnoreMatcher: a .git pointer file's gitdir is followed for info/exclude (worktrees)", "[GitIgnore]") {
    TempDir              dir("ned_gitignore_test_gitdir_pointer");
    const HermeticGitEnv env(dir.path);
    std::filesystem::create_directories(dir.path / "gitdir-target" / "info");
    {
        std::ofstream(dir.path / "gitdir-target" / "info" / "exclude") << "*.bak\n";
    }
    {
        std::ofstream(dir.path / ".git") << "gitdir: gitdir-target\n";
    }

    const GitIgnoreMatcher matcher(dir.path);
    CHECK(matcher.IsIgnored("old.bak", false));
}

TEST_CASE("GitIgnoreMatcher: the default global ignore file applies inside a git repository only", "[GitIgnore]") {
    TempDir              dir("ned_gitignore_test_global_default");
    const HermeticGitEnv env(dir.path);
    std::filesystem::create_directories(env.xdgDir / "git");
    {
        std::ofstream(env.xdgDir / "git" / "ignore") << "*.swp\n";
    }
    std::filesystem::create_directories(dir.path / "repo" / ".git");
    std::filesystem::create_directories(dir.path / "plain");

    const GitIgnoreMatcher inRepo(dir.path / "repo");
    CHECK(inRepo.IsIgnored("x.swp", false));

    const GitIgnoreMatcher outsideRepo(dir.path / "plain");
    CHECK_FALSE(outsideRepo.IsIgnored("x.swp", false)); // no .git -- global rules don't apply (git's own rule)
}

TEST_CASE("GitIgnoreMatcher: core.excludesFile from ~/.gitconfig overrides the default global path", "[GitIgnore]") {
    TempDir              dir("ned_gitignore_test_excludesfile");
    const HermeticGitEnv env(dir.path);
    {
        std::ofstream(env.homeDir / ".gitconfig") << "[core]\n\texcludesFile = ~/custom-ignore\n";
    }
    {
        std::ofstream(env.homeDir / "custom-ignore") << "*.foo\n";
    }
    std::filesystem::create_directories(dir.path / "repo" / ".git");

    const GitIgnoreMatcher matcher(dir.path / "repo");
    CHECK(matcher.IsIgnored("x.foo", false));
}

TEST_CASE("GitIgnoreMatcher: '**' follows git's own semantics", "[GitIgnore]") {
    TempDir dir("ned_gitignore_test_doublestar");
    {
        std::ofstream(dir.path / ".gitignore") << "docs/**\n**/vendor\na/**/b\n";
    }

    const GitIgnoreMatcher matcher(dir.path);
    CHECK(matcher.IsIgnored("docs/index.html", false)); // trailing "/**": everything inside
    CHECK(matcher.IsIgnored("docs/sub", true));
    CHECK_FALSE(matcher.IsIgnored("docs", true)); // ...but not the directory itself
    CHECK(matcher.IsIgnored("vendor", true));     // leading "**/": any depth, including the root
    CHECK(matcher.IsIgnored("x/y/vendor", true));
    CHECK(matcher.IsIgnored("a/b", false)); // interior "/**/": zero or more directories
    CHECK(matcher.IsIgnored("a/x/y/b", false));
    CHECK_FALSE(matcher.IsIgnored("a/xb", false));
}

TEST_CASE("GitIgnoreMatcher: character classes match like fnmatch", "[GitIgnore]") {
    TempDir dir("ned_gitignore_test_classes");
    {
        std::ofstream(dir.path / ".gitignore") << "*.[oa]\n[!a]*.txt\ntmp-[0-9]*\n";
    }

    const GitIgnoreMatcher matcher(dir.path);
    CHECK(matcher.IsIgnored("x.o", false));
    CHECK(matcher.IsIgnored("x.a", false));
    CHECK_FALSE(matcher.IsIgnored("x.c", false));
    CHECK(matcher.IsIgnored("b1.txt", false));
    CHECK_FALSE(matcher.IsIgnored("a1.txt", false)); // negated class
    CHECK(matcher.IsIgnored("tmp-3x", false));       // range
    CHECK_FALSE(matcher.IsIgnored("tmp-x", false));
}

// Note: no literal '[' in the test *name* -- catch_discover_tests' generated
// filter can't round-trip one (the whole ctest shard then matches nothing).
TEST_CASE("GitIgnoreMatcher: an unterminated opening bracket matches literally", "[GitIgnore]") {
    TempDir dir("ned_gitignore_test_unterminated_class");
    {
        std::ofstream(dir.path / ".gitignore") << "foo[bar\n";
    }

    const GitIgnoreMatcher matcher(dir.path);
    CHECK(matcher.IsIgnored("foo[bar", false));
    CHECK_FALSE(matcher.IsIgnored("foob", false));
}

TEST_CASE("CachedGitIgnoreMatcher picks up a nested .gitignore created after caching", "[GitIgnore]") {
    TempDir dir("ned_gitignore_test_cache_nested");
    std::filesystem::create_directories(dir.path / "sub");

    // The first query records sub/.gitignore's absence as a consulted
    // source -- what lets its later appearance invalidate the cache.
    CHECK_FALSE(CachedGitIgnoreMatcher(dir.path).IsIgnored("sub/x.log", false));

    {
        std::ofstream(dir.path / "sub" / ".gitignore") << "*.log\n";
    }
    CHECK(CachedGitIgnoreMatcher(dir.path).IsIgnored("sub/x.log", false));
}

TEST_CASE("CachedGitIgnoreMatcher rebuilds when the root .gitignore changes", "[GitIgnore]") {
    TempDir dir("ned_gitignore_test_cache_root");
    {
        std::ofstream(dir.path / ".gitignore") << "*.log\n";
    }
    CHECK(CachedGitIgnoreMatcher(dir.path).IsIgnored("x.log", false));

    {
        std::ofstream(dir.path / ".gitignore") << "*.tmp\n";
    }
    CHECK_FALSE(CachedGitIgnoreMatcher(dir.path).IsIgnored("x.log", false));
    CHECK(CachedGitIgnoreMatcher(dir.path).IsIgnored("x.tmp", false));
}
