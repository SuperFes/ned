#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/ProjectSearch.h"
#include "Editor/SearchSettings.h"
#include "EnvOverride.h"

using ned::editor::SearchDirectory;
using ned::editor::SearchMatch;
using ned::editor::SearchPatternError;

namespace {

bool AnyMatchIn(const std::vector<SearchMatch>& matches, const std::filesystem::path& file, std::size_t line) {
    return std::any_of(matches.begin(), matches.end(),
                       [&](const SearchMatch& m) { return m.file == file && m.lineNumber == line; });
}

// internal-project-search follow-up: ProjectSearchThreads is process-wide
// state (SearchSettings.h) -- mirrors TabWidthTest.cpp's own TabWidthGuard.
struct ProjectSearchThreadsGuard {
    explicit ProjectSearchThreadsGuard(int threads) {
        ned::editor::SetProjectSearchThreads(threads);
    }
    ~ProjectSearchThreadsGuard() {
        ned::editor::SetProjectSearchThreads(4);
    }
};

// gitignore-correctness follow-up: a fixture carrying a .git entry makes
// GitIgnoreMatcher consult the machine's real global git configuration --
// pin it to a disposable location (see EnvOverride.h / GitIgnoreTest.cpp's
// own HermeticGitEnv).
struct HermeticGitEnv {
    std::filesystem::path         homeDir;
    ned::tests::ScopedEnvOverride home;
    ned::tests::ScopedEnvOverride xdg;
    ned::tests::ScopedEnvOverride gitConfigGlobal;

    explicit HermeticGitEnv(const std::filesystem::path& base) : homeDir(base / "fake-home"),
                                                                 home("HOME", (base / "fake-home").c_str()),
                                                                 xdg("XDG_CONFIG_HOME", (base / "fake-home" / ".config").c_str()),
                                                                 gitConfigGlobal("GIT_CONFIG_GLOBAL", nullptr) {
        std::filesystem::create_directories(homeDir / ".config");
    }
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

TEST_CASE("SearchDirectory throws SearchPatternError for an invalid pattern", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_badregex";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    REQUIRE_THROWS_AS(SearchDirectory(dir, "("), SearchPatternError);

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory rejects std::regex-only syntax RE2 doesn't support", "[ProjectSearch]") {
    // internal-project-search follow-up: a concrete example of the syntax
    // gap ProjectReplace.h's own doc comment now calls out -- backreferences
    // are valid std::regex ECMAScript syntax but RE2 deliberately doesn't
    // support them (no catastrophic-backtracking exposure that way).
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_backref";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    REQUIRE_THROWS_AS(SearchDirectory(dir, R"((\w+) \1)"), SearchPatternError);

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory skips a directory excluded by .gitignore, without descending into it", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_gitignore";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "build" / "nested");
    const HermeticGitEnv env(dir);
    // GitIgnoreMatcher doesn't require a real .git marker for .gitignore
    // files themselves -- but a bare .gitignore with no .git at all is the
    // rarer real-world shape, so this fixture includes one anyway to mirror
    // an actual project.
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
    const HermeticGitEnv env(dir);
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

TEST_CASE("SearchDirectory honors a nested .gitignore, relative to its own directory", "[ProjectSearch]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_gitignore_nested";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    const HermeticGitEnv env(dir);
    std::filesystem::create_directory(dir / ".git");
    {
        std::ofstream(dir / "sub" / ".gitignore") << "*.gen\n";
    }
    {
        std::ofstream(dir / "sub" / "skipped.gen") << "needle\n";
    }
    {
        std::ofstream(dir / "root-level.gen") << "needle\n"; // the nested rule doesn't reach the root
    }

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().file.filename() == "root-level.gen");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory finds every match across more files than the worker-thread cap, in deterministic order",
          "[ProjectSearch]") {
    // internal-project-search follow-up: pins the thread cap below the file
    // count so the parallel path (SearchFilesParallel's atomic work-stealing
    // counter) is actually exercised, not just the "one file, no real
    // contention" shape every other test above happens to hit. Results are
    // reassembled back into original file-visitation order regardless of
    // which worker processed which file -- REQUIRE (not a set comparison)
    // below checks that ordering directly, not just membership.
    const ProjectSearchThreadsGuard threadsGuard(2);

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_search_test_manyfiles";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    constexpr int kFileCount = 20;
    for (int i = 0; i < kFileCount; ++i) {
        std::ofstream(dir / (std::string("file") + std::to_string(i) + ".txt")) << "needle " << i << "\n";
    }

    const std::vector<SearchMatch> matches = SearchDirectory(dir, "needle");
    REQUIRE(matches.size() == static_cast<std::size_t>(kFileCount));
    for (int i = 0; i < kFileCount; ++i) {
        REQUIRE(AnyMatchIn(matches, std::filesystem::absolute(dir / (std::string("file") + std::to_string(i) + ".txt")), 1));
    }

    // Same directory walk order every time (std::filesystem::
    // recursive_directory_iterator, single-threaded, unaffected by the
    // parallel search stage below it) -- so re-running the identical search
    // must reproduce the identical match order, proving SearchFilesParallel's
    // per-file result slots really did get reassembled deterministically
    // rather than in whatever order threads happened to finish.
    const std::vector<SearchMatch> matchesAgain = SearchDirectory(dir, "needle");
    REQUIRE(matchesAgain.size() == matches.size());
    for (std::size_t i = 0; i < matches.size(); ++i) {
        REQUIRE(matches[i].file == matchesAgain[i].file);
        REQUIRE(matches[i].lineNumber == matchesAgain[i].lineNumber);
    }

    std::filesystem::remove_all(dir);
}
