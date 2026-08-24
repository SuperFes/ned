#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Process/ChildProcess.h"
#include "Editor/ToolchainIncludePaths.h"

using ned::editor::ClearToolchainIncludePathCache;
using ned::editor::IncludePathCacheTtlSeconds;
using ned::editor::QueryToolchainIncludePaths;
using ned::editor::SetIncludePathCacheTtlSeconds;
using ned::editor::ToolchainIncludePathsForLanguage;

namespace {

// Mirrors BackupTest.cpp/SessionTest.cpp's own EnvVarGuard exactly -- not
// worth a shared header for something this small (same precedent those two
// files' own header comments already establish for this exact class).
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
    std::string previous_;
    bool        hadPrevious_ = false;
};

// One disposable sandbox per test: a temp root serving as XDG_CACHE_HOME, so
// the cache file lands inside it rather than a real user's ~/.cache/ned.
struct CacheSandbox {
    std::filesystem::path root;
    EnvVarGuard           guard;

    CacheSandbox() : root(std::filesystem::temp_directory_path() / "ned-toolchain-include-paths-test"), guard("XDG_CACHE_HOME", root.string().c_str()) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~CacheSandbox() {
        std::filesystem::remove_all(root);
    }

    [[nodiscard]] std::filesystem::path CacheFile() const {
        return root / "ned" / "toolchain-include-paths.json";
    }

    void WriteCacheEntry(const std::string& language, const std::vector<std::string>& paths, std::int64_t probedAt) const {
        std::filesystem::create_directories(CacheFile().parent_path());
        nlohmann::json pathArray = nlohmann::json::array();
        for (const std::string& path : paths) {
            pathArray.push_back(path);
        }
        const nlohmann::json cache = nlohmann::json{{language, nlohmann::json{{"probedAt", probedAt}, {"paths", pathArray}}}};
        std::ofstream        file(CacheFile(), std::ios::binary | std::ios::trunc);
        file << cache.dump();
    }
};

// Restores the process-wide TTL to its documented default around any test
// that changes it -- this is mutable state shared with every other test
// case in this same ned_tests binary.
struct TtlGuard {
    int previous = IncludePathCacheTtlSeconds();
    ~TtlGuard() {
        SetIncludePathCacheTtlSeconds(previous);
    }
};

std::int64_t NowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

TEST_CASE("QueryToolchainIncludePaths returns nullopt for an unsupported language", "[ToolchainIncludePaths]") {
    CHECK_FALSE(QueryToolchainIncludePaths("python").has_value());
    CHECK_FALSE(QueryToolchainIncludePaths("php").has_value());
    CHECK_FALSE(QueryToolchainIncludePaths("javascript").has_value());
}

TEST_CASE("QueryToolchainIncludePaths finds the real compiler's system include search paths for cpp",
          "[ToolchainIncludePaths]") {
    if (!ned::editor::process::ResolveExecutable("c++")) {
        SKIP("c++ not found on $PATH");
    }

    const auto paths = QueryToolchainIncludePaths("cpp");
    REQUIRE(paths.has_value());
    CHECK_FALSE(paths->empty());

    // At least one reported search path must actually exist on this
    // machine -- otherwise the parser is picking up the wrong block of
    // -v output entirely.
    bool anyExists = false;
    for (const std::filesystem::path& path : *paths) {
        if (std::filesystem::exists(path)) {
            anyExists = true;
            break;
        }
    }
    CHECK(anyExists);
}

TEST_CASE("QueryToolchainIncludePaths kills the compiler and returns nullopt when it exceeds readTimeout",
          "[ToolchainIncludePaths]") {
    // subprocess-hang-protection follow-up. Uses the real compiler with an
    // unreasonably short readTimeout (0ms) rather than a fake hanging tool --
    // CompilerAndLanguageFlagFor hardcodes "c++"/"cc", so there's no seam to
    // inject a substitute argv the way Clipboard's SetClipboardPasteCommand
    // allows.
    if (!ned::editor::process::ResolveExecutable("c++")) {
        SKIP("c++ not found on $PATH");
    }

    REQUIRE_FALSE(QueryToolchainIncludePaths("cpp", std::chrono::milliseconds(0)).has_value());
}

TEST_CASE("ToolchainIncludePathsForLanguage returns an empty list for an unsupported language", "[ToolchainIncludePaths]") {
    CacheSandbox sandbox;
    CHECK(ToolchainIncludePathsForLanguage("python").empty());
}

TEST_CASE("ToolchainIncludePathsForLanguage serves a fresh cached entry without re-probing", "[ToolchainIncludePaths]") {
    CacheSandbox sandbox;
    TtlGuard     ttlGuard;
    SetIncludePathCacheTtlSeconds(86400);

    // A path a real compiler would never report -- proves this came from
    // the cache, not a live probe.
    sandbox.WriteCacheEntry("cpp", {"/totally/fake/only-in-cache"}, NowUnixSeconds());

    const auto paths = ToolchainIncludePathsForLanguage("cpp");
    REQUIRE(paths.size() == 1);
    CHECK(paths[0] == std::filesystem::path("/totally/fake/only-in-cache"));
}

TEST_CASE("ToolchainIncludePathsForLanguage re-probes once a cached entry has expired", "[ToolchainIncludePaths]") {
    if (!ned::editor::process::ResolveExecutable("c++")) {
        SKIP("c++ not found on $PATH");
    }

    CacheSandbox sandbox;
    TtlGuard     ttlGuard;
    SetIncludePathCacheTtlSeconds(1);

    sandbox.WriteCacheEntry("cpp", {"/totally/fake/only-in-cache"}, NowUnixSeconds() - 999999);

    const auto paths = ToolchainIncludePathsForLanguage("cpp");
    REQUIRE_FALSE(paths.empty());
    CHECK(paths[0] != std::filesystem::path("/totally/fake/only-in-cache"));
}

TEST_CASE("ToolchainIncludePathsForLanguage treats a TTL of 0 as caching disabled", "[ToolchainIncludePaths]") {
    if (!ned::editor::process::ResolveExecutable("c++")) {
        SKIP("c++ not found on $PATH");
    }

    CacheSandbox sandbox;
    TtlGuard     ttlGuard;
    SetIncludePathCacheTtlSeconds(0);

    // Even a just-written, technically-fresh entry must be ignored.
    sandbox.WriteCacheEntry("cpp", {"/totally/fake/only-in-cache"}, NowUnixSeconds());

    const auto paths = ToolchainIncludePathsForLanguage("cpp");
    REQUIRE_FALSE(paths.empty());
    CHECK(paths[0] != std::filesystem::path("/totally/fake/only-in-cache"));
}

TEST_CASE("ClearToolchainIncludePathCache removes the cache file, and is a no-op when none exists", "[ToolchainIncludePaths]") {
    CacheSandbox sandbox;
    sandbox.WriteCacheEntry("cpp", {"/whatever"}, NowUnixSeconds());
    REQUIRE(std::filesystem::exists(sandbox.CacheFile()));

    ClearToolchainIncludePathCache();
    CHECK_FALSE(std::filesystem::exists(sandbox.CacheFile()));

    ClearToolchainIncludePathCache(); // no file left -- must not throw
    SUCCEED();
}

TEST_CASE("SetIncludePathCacheTtlSeconds/IncludePathCacheTtlSeconds round-trip", "[ToolchainIncludePaths]") {
    TtlGuard ttlGuard;

    SetIncludePathCacheTtlSeconds(3600);
    CHECK(IncludePathCacheTtlSeconds() == 3600);

    SetIncludePathCacheTtlSeconds(0);
    CHECK(IncludePathCacheTtlSeconds() == 0);
}
