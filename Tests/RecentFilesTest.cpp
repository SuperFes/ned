#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/RecentFiles.h"
#include "Text/Buffer.h"

using ned::editor::RecentFilePaths;
using ned::editor::RecentFilesEnabled;
using ned::editor::RecentFilesPath;
using ned::editor::RecentFilesStore;
using ned::editor::RecordRecentFile;
using ned::editor::ResetRecentFilesForTesting;
using ned::editor::SetRecentFilesEnabled;

namespace {

// SessionTest.cpp's own EnvVarGuard, duplicated per that file's precedent.
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

struct RecentFilesStateGuard {
    RecentFilesStateGuard() {
        ResetRecentFilesForTesting();
    }
    ~RecentFilesStateGuard() {
        ResetRecentFilesForTesting();
    }
};

std::filesystem::path FreshTestDir(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path WriteTestFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
    return path;
}

} // namespace

TEST_CASE("RecentFilesStore orders paths most-recent-first", "[RecentFiles]") {
    const std::filesystem::path dir = FreshTestDir("ned_recentfiles_test_order");
    const std::filesystem::path a   = WriteTestFile(dir / "a.txt", "a\n");
    const std::filesystem::path b   = WriteTestFile(dir / "b.txt", "b\n");
    const std::filesystem::path c   = WriteTestFile(dir / "c.txt", "c\n");

    RecentFilesStore store;
    store.Record(a, 1);
    store.Record(b, 2);
    store.Record(c, 3);

    const std::vector<std::string> paths = store.Paths();
    REQUIRE(paths.size() == 3);
    REQUIRE(paths[0] == RecentFilesStore::NormalizePathKey(c));
    REQUIRE(paths[1] == RecentFilesStore::NormalizePathKey(b));
    REQUIRE(paths[2] == RecentFilesStore::NormalizePathKey(a));
}

TEST_CASE("RecentFilesStore re-recording moves a path to the front, not a duplicate", "[RecentFiles]") {
    const std::filesystem::path dir = FreshTestDir("ned_recentfiles_test_dedupe");
    const std::filesystem::path a   = WriteTestFile(dir / "a.txt", "a\n");
    const std::filesystem::path b   = WriteTestFile(dir / "b.txt", "b\n");

    RecentFilesStore store;
    store.Record(a, 1);
    store.Record(b, 2);
    store.Record(a, 3); // re-opened a -- moves to the front, no duplicate entry

    REQUIRE(store.Count() == 2);
    const std::vector<std::string> paths = store.Paths();
    REQUIRE(paths[0] == RecentFilesStore::NormalizePathKey(a));
    REQUIRE(paths[1] == RecentFilesStore::NormalizePathKey(b));
}

TEST_CASE("RecentFilesStore JSON round-trips", "[RecentFiles]") {
    const std::filesystem::path dir = FreshTestDir("ned_recentfiles_test_json");
    const std::filesystem::path a   = WriteTestFile(dir / "a.txt", "a\n");

    RecentFilesStore store;
    store.Record(a, 5);

    const RecentFilesStore loaded = RecentFilesStore::FromJson(store.ToJson());
    REQUIRE(loaded.Count() == 1);
    REQUIRE(loaded.Paths() == std::vector<std::string>{RecentFilesStore::NormalizePathKey(a)});
    REQUIRE_FALSE(loaded.Dirty());
}

TEST_CASE("RecentFilesStore tolerates malformed JSON and malformed entries", "[RecentFiles]") {
    REQUIRE(RecentFilesStore::FromJson("not json at all").Count() == 0);
    REQUIRE(RecentFilesStore::FromJson("{}").Count() == 0);
    const RecentFilesStore store =
        RecentFilesStore::FromJson(R"({"version":1,"files":[{"lastUsed":1},{"path":"/tmp/x","lastUsed":2}]})");
    REQUIRE(store.Count() == 1);
}

TEST_CASE("RecentFilesStore evicts the least-recently-used entry past the cap", "[RecentFiles]") {
    const std::filesystem::path dir = FreshTestDir("ned_recentfiles_test_lru");

    RecentFilesStore store;
    for (std::size_t i = 0; i <= RecentFilesStore::kMaxEntries; ++i) {
        store.Record(dir / ("f" + std::to_string(i) + ".txt"), static_cast<std::int64_t>(i));
    }

    REQUIRE(store.Count() == RecentFilesStore::kMaxEntries);
    const std::vector<std::string> paths = store.Paths();
    REQUIRE(paths.back() == RecentFilesStore::NormalizePathKey(dir / "f1.txt")); // f0 evicted
}

TEST_CASE("RecentFilesStore saves to and loads from a file", "[RecentFiles]") {
    const std::filesystem::path dir       = FreshTestDir("ned_recentfiles_test_file");
    const std::filesystem::path a         = WriteTestFile(dir / "a.txt", "a\n");
    const std::filesystem::path storePath = dir / "state" / "recent-files.json"; // parent must be created

    RecentFilesStore store;
    store.Record(a, 1);
    store.SaveToFile(storePath);

    RecentFilesStore loaded;
    loaded.LoadFromFile(storePath);
    REQUIRE(loaded.Count() == 1);

    loaded.LoadFromFile(dir / "does-not-exist.json");
    REQUIRE(loaded.Count() == 0);
}

TEST_CASE("RecentFilesPath prefers XDG_STATE_HOME, falls back to HOME", "[RecentFiles]") {
    {
        EnvVarGuard xdg("XDG_STATE_HOME", "/tmp/ned-xdg-test-state");
        EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");
        REQUIRE(RecentFilesPath() == std::filesystem::path("/tmp/ned-xdg-test-state/ned/recent-files.json"));
    }
    {
        EnvVarGuard xdg("XDG_STATE_HOME", nullptr);
        EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");
        REQUIRE(RecentFilesPath() ==
                std::filesystem::path("/tmp/ned-xdg-test-home/.local/state/ned/recent-files.json"));
    }
}

TEST_CASE("RecordRecentFile and RecentFilePaths round-trip through a Buffer", "[RecentFiles]") {
    RecentFilesStateGuard       guard;
    const std::filesystem::path dir = FreshTestDir("ned_recentfiles_test_buffer");
    const std::filesystem::path a   = WriteTestFile(dir / "a.txt", "alpha\n");
    const std::filesystem::path b   = WriteTestFile(dir / "b.txt", "bravo\n");

    ned::text::Buffer bufferA = ned::text::Buffer::FromFile(a);
    ned::text::Buffer bufferB = ned::text::Buffer::FromFile(b);
    RecordRecentFile(bufferA);
    RecordRecentFile(bufferB);

    // Both calls can land in the same wall-clock second (RecordRecentFile
    // has no test-only timestamp injection, unlike RecentFilesStore::Record
    // directly -- see the ordering tests above for that), so only presence
    // is checked here, not relative order.
    const std::vector<std::string> paths = RecentFilePaths();
    REQUIRE(paths.size() == 2);
    REQUIRE(std::find(paths.begin(), paths.end(), RecentFilesStore::NormalizePathKey(a)) != paths.end());
    REQUIRE(std::find(paths.begin(), paths.end(), RecentFilesStore::NormalizePathKey(b)) != paths.end());
}

TEST_CASE("Disabled recent-files turns record and query into no-ops", "[RecentFiles]") {
    RecentFilesStateGuard       guard;
    const std::filesystem::path dir = FreshTestDir("ned_recentfiles_test_disabled");
    const std::filesystem::path a   = WriteTestFile(dir / "a.txt", "alpha\n");

    REQUIRE(RecentFilesEnabled());
    SetRecentFilesEnabled(false);
    REQUIRE_FALSE(RecentFilesEnabled());

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(a);
    RecordRecentFile(buffer);
    REQUIRE(RecentFilePaths().empty());
}

TEST_CASE("A pathless buffer never records", "[RecentFiles]") {
    RecentFilesStateGuard guard;

    ned::text::Buffer scratch("scratch");
    scratch.InsertAtPoint("some text");
    RecordRecentFile(scratch);
    REQUIRE(RecentFilePaths().empty());
}
