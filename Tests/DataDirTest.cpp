#include "Editor/DataDir.h"

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

namespace fs = std::filesystem;
using ned::editor::DataDir;
using ned::editor::DataDirCandidates;
using ned::editor::ResolveDataDir;

namespace {

std::atomic<int> g_counter{0};

struct TempTree {
    fs::path root;
    TempTree() : root(fs::temp_directory_path() / ("ned-datadir-" + std::to_string(getpid()) + "-" + std::to_string(g_counter++))) {
        fs::create_directories(root);
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    // A directory that holds a data tree, or a bare one that doesn't.
    fs::path Dir(const char* name, bool withLanguages) {
        const fs::path dir = root / name;
        fs::create_directories(withLanguages ? dir / "languages" : dir);
        return dir;
    }
};

} // namespace

TEST_CASE("NED_DATA_DIR is authoritative: used when valid, an error when not", "[DataDir]") {
    TempTree       tree;
    const fs::path env = tree.Dir("env", true);
    const fs::path exe = tree.Dir("exe", true);

    CHECK(ResolveDataDir({.environment = env, .executableRelative = exe, .installed = exe}) == env);

    const fs::path bad = tree.Dir("bad", false);
    CHECK_THROWS_WITH(ResolveDataDir({.environment = bad, .executableRelative = exe, .installed = exe}),
                      Catch::Matchers::ContainsSubstring(bad.string()));
}

TEST_CASE("Without NED_DATA_DIR the executable-relative tree wins over the installed one", "[DataDir]") {
    TempTree       tree;
    const fs::path exe       = tree.Dir("exe", true);
    const fs::path installed = tree.Dir("installed", true);

    CHECK(ResolveDataDir({.executableRelative = exe, .installed = installed}) == exe);
    CHECK(ResolveDataDir({.executableRelative = tree.Dir("missing", false), .installed = installed}) == installed);
    CHECK(ResolveDataDir({.installed = installed}) == installed);
}

TEST_CASE("No data tree anywhere names every place looked", "[DataDir]") {
    TempTree       tree;
    const fs::path exe       = tree.Dir("exe", false);
    const fs::path installed = tree.Dir("installed", false);
    CHECK_THROWS_WITH(ResolveDataDir({.executableRelative = exe, .installed = installed}),
                      Catch::Matchers::ContainsSubstring(exe.string()) &&
                          Catch::Matchers::ContainsSubstring(installed.string()));
}

TEST_CASE("The test binary resolves the build tree's share/ned", "[DataDir]") {
    CHECK(fs::is_directory(DataDir() / "languages" / "cpp"));
    CHECK(fs::is_regular_file(DataDir() / "plugins" / "languages.janet"));
}
