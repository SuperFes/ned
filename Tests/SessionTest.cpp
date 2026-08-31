#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Session.h"
#include "Text/Buffer.h"

using ned::editor::FilePlace;
using ned::editor::FilePlacesPath;
using ned::editor::FilePlaceStore;
using ned::editor::RecordFilePlace;
using ned::editor::ResetFilePlacesForTesting;
using ned::editor::RestoreFilePlace;
using ned::editor::SavePlaceEnabled;
using ned::editor::SetSavePlaceEnabled;
using ned::editor::StoredFilePlaceFor;

namespace {

// Mirrors InitFileTest.cpp's own EnvVarGuard exactly (see ScratchPadTest.cpp
// for the same duplication precedent).
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

// The process-wide store/toggle are static state (see Session.h) -- every
// test touching them restores the defaults, same AutoSaveGuard precedent
// ScratchPadTest.cpp established.
struct SessionStateGuard {
    SessionStateGuard() {
        ResetFilePlacesForTesting();
    }
    ~SessionStateGuard() {
        ResetFilePlacesForTesting();
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

TEST_CASE("FilePlaceStore records and looks places back up", "[Session]") {
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_roundtrip");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "one\ntwo\nthree\n");

    FilePlaceStore store;
    REQUIRE_FALSE(store.Lookup(file).has_value());

    store.Record(file, FilePlace{.line = 2, .column = 3, .topLine = 1});

    const auto place = store.Lookup(file);
    REQUIRE(place.has_value());
    REQUIRE(place->line == 2);
    REQUIRE(place->column == 3);
    REQUIRE(place->topLine == 1);
    REQUIRE(store.Count() == 1);
}

TEST_CASE("FilePlaceStore normalizes path spellings onto one entry", "[Session]") {
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_normalize");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "text\n");

    FilePlaceStore store;
    store.Record(file, FilePlace{.line = 1, .column = 0});

    // A dot-segmented spelling of the same file must land on the same entry.
    const std::filesystem::path dotted = dir / "." / "a.txt";
    const auto                  place  = store.Lookup(dotted);
    REQUIRE(place.has_value());
    REQUIRE(place->line == 1);

    store.Record(dotted, FilePlace{.line = 5, .column = 2});
    REQUIRE(store.Count() == 1);
    REQUIRE(store.Lookup(file)->line == 5);
}

TEST_CASE("FilePlaceStore merges a nullopt topLine with the stored one", "[Session]") {
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_merge");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "text\n");

    FilePlaceStore store;
    store.Record(file, FilePlace{.line = 10, .column = 0, .topLine = 7});
    // A background-buffer record (no viewport) must not wipe the viewport.
    store.Record(file, FilePlace{.line = 12, .column = 4, .topLine = std::nullopt});

    const auto place = store.Lookup(file);
    REQUIRE(place->line == 12);
    REQUIRE(place->column == 4);
    REQUIRE(place->topLine == 7);
}

TEST_CASE("FilePlaceStore JSON round-trips, including an absent topLine", "[Session]") {
    const std::filesystem::path dir     = FreshTestDir("ned_session_test_json");
    const std::filesystem::path withTop = WriteTestFile(dir / "a.txt", "text\n");
    const std::filesystem::path noTop   = WriteTestFile(dir / "b.txt", "text\n");

    FilePlaceStore store;
    store.Record(withTop, FilePlace{.line = 3, .column = 1, .topLine = 2});
    store.Record(noTop, FilePlace{.line = 9, .column = 0});

    const FilePlaceStore loaded = FilePlaceStore::FromJson(store.ToJson());
    REQUIRE(loaded.Count() == 2);
    REQUIRE(loaded.Lookup(withTop) == FilePlace{.line = 3, .column = 1, .topLine = 2});
    REQUIRE(loaded.Lookup(noTop) == FilePlace{.line = 9, .column = 0, .topLine = std::nullopt});
    REQUIRE_FALSE(loaded.Dirty());
}

TEST_CASE("FilePlaceStore tolerates malformed JSON and malformed entries", "[Session]") {
    REQUIRE(FilePlaceStore::FromJson("not json at all").Count() == 0);
    REQUIRE(FilePlaceStore::FromJson("{}").Count() == 0);
    // One entry missing its path must not discard the valid one beside it.
    const FilePlaceStore store = FilePlaceStore::FromJson(
        R"({"version":1,"places":[{"line":1},{"path":"/tmp/x","line":4,"column":2,"lastUsed":10}]})");
    REQUIRE(store.Count() == 1);
}

TEST_CASE("FilePlaceStore evicts the least-recently-used entry past the cap", "[Session]") {
    const std::filesystem::path dir = FreshTestDir("ned_session_test_lru");

    FilePlaceStore store;
    // Timestamps are injected: entry 0 is the oldest, so it must be the one
    // evicted when kMaxEntries + 1 entries exist. The paths don't need to
    // exist -- NormalizePathKey falls back to absolute() for them.
    for (std::size_t i = 0; i <= FilePlaceStore::kMaxEntries; ++i) {
        store.Record(dir / ("f" + std::to_string(i) + ".txt"), FilePlace{.line = i},
                     static_cast<std::int64_t>(i));
    }

    REQUIRE(store.Count() == FilePlaceStore::kMaxEntries);
    REQUIRE_FALSE(store.Lookup(dir / "f0.txt").has_value());
    REQUIRE(store.Lookup(dir / "f1.txt").has_value());
}

TEST_CASE("FilePlaceStore saves to and loads from a file", "[Session]") {
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_file");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "text\n");
    // Nested path: SaveToFile must create parent directories itself.
    const std::filesystem::path storePath = dir / "state" / "file-places.json";

    FilePlaceStore store;
    store.Record(file, FilePlace{.line = 4, .column = 2, .topLine = 3});
    store.SaveToFile(storePath);

    FilePlaceStore loaded;
    loaded.LoadFromFile(storePath);
    REQUIRE(loaded.Count() == 1);
    REQUIRE(loaded.Lookup(file) == FilePlace{.line = 4, .column = 2, .topLine = 3});

    // A missing file loads as empty, never throws.
    loaded.LoadFromFile(dir / "does-not-exist.json");
    REQUIRE(loaded.Count() == 0);
}

TEST_CASE("Dirty tracks place changes, not lastUsed bumps", "[Session]") {
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_dirty");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "text\n");

    FilePlaceStore store;
    REQUIRE_FALSE(store.Dirty());

    store.Record(file, FilePlace{.line = 1, .column = 0});
    REQUIRE(store.Dirty());

    store.ClearDirty();
    store.Record(file, FilePlace{.line = 1, .column = 0}); // identical place
    REQUIRE_FALSE(store.Dirty());
    store.Touch(file);
    REQUIRE_FALSE(store.Dirty());

    store.Record(file, FilePlace{.line = 2, .column = 0});
    REQUIRE(store.Dirty());
}

TEST_CASE("FilePlacesPath prefers XDG_STATE_HOME, falls back to HOME", "[Session]") {
    {
        EnvVarGuard xdg("XDG_STATE_HOME", "/tmp/ned-xdg-test-state");
        EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");
        REQUIRE(FilePlacesPath() == std::filesystem::path("/tmp/ned-xdg-test-state/ned/file-places.json"));
    }
    {
        EnvVarGuard xdg("XDG_STATE_HOME", nullptr);
        EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");
        REQUIRE(FilePlacesPath() ==
                std::filesystem::path("/tmp/ned-xdg-test-home/.local/state/ned/file-places.json"));
    }
}

TEST_CASE("RecordFilePlace and RestoreFilePlace round-trip through a Buffer", "[Session]") {
    SessionStateGuard           guard;
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_buffer");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "alpha\nbravo\ncharlie\n");

    ned::text::Buffer recorded = ned::text::Buffer::FromFile(file);
    recorded.SetPoint(recorded.ByteOffsetForLineAndColumn(2, 4)); // "charlie", column 4
    RecordFilePlace(recorded, 1, /*tabWidth=*/4);

    const auto stored = StoredFilePlaceFor(recorded);
    REQUIRE(stored == FilePlace{.line = 2, .column = 4, .topLine = 1});

    ned::text::Buffer restored = ned::text::Buffer::FromFile(file);
    REQUIRE(restored.Point() == 0);
    RestoreFilePlace(restored, /*tabWidth=*/4);
    REQUIRE(restored.Point() == restored.ByteOffsetForLineAndColumn(2, 4));
}

TEST_CASE("RecordFilePlace and RestoreFilePlace round-trip through a huge Buffer",
          "[Session][HugeFile]") {
    // huge-file-navigation-verification follow-up: RecordFilePlace/
    // RestoreFilePlace never touch buffer.Text() -- both go through
    // Buffer::ByteOffsetForLineAndColumn/VisualColumnForByteOffset, which
    // are themselves bounded (Content()'s O(log n) line index plus a
    // kMaxTabAwareColumnScan-capped per-line scan, see Buffer.cpp) -- so
    // this is a correctness round-trip, mirroring the FromFile test above
    // exactly, just against a real huge (piece-table-backed) buffer via
    // Buffer::FromHugeFile (BufferHugeFileTest.cpp's own precedent: small
    // content is fine, FromHugeFile doesn't itself check size). A line deep
    // in the file (not line 0) is the point of the test -- proves the
    // round-trip isn't accidentally only correct for content already near
    // the start.
    SessionStateGuard           guard;
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_huge_buffer");
    std::string                 content;
    for (int i = 0; i < 2000; ++i) {
        content += "line " + std::to_string(i) + "\n";
    }
    const std::filesystem::path file = WriteTestFile(dir / "huge.txt", content);

    ned::text::Buffer recorded = ned::text::Buffer::FromHugeFile(file);
    REQUIRE(recorded.Content().IsHuge());
    recorded.SetPoint(recorded.ByteOffsetForLineAndColumn(1500, 3));
    RecordFilePlace(recorded, /*topLine=*/1490, /*tabWidth=*/4);

    const auto stored = StoredFilePlaceFor(recorded);
    REQUIRE(stored == FilePlace{.line = 1500, .column = 3, .topLine = 1490});

    ned::text::Buffer restored = ned::text::Buffer::FromHugeFile(file);
    REQUIRE(restored.Point() == 0);
    RestoreFilePlace(restored, /*tabWidth=*/4);
    REQUIRE(restored.Point() == restored.ByteOffsetForLineAndColumn(1500, 3));
}

TEST_CASE("RestoreFilePlace clamps a place past the file's current end", "[Session]") {
    SessionStateGuard           guard;
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_clamp");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "long\nfile\nwith\nlines\n");

    ned::text::Buffer recorded = ned::text::Buffer::FromFile(file);
    recorded.SetPoint(recorded.ByteOffsetForLineAndColumn(3, 2));
    RecordFilePlace(recorded, std::nullopt, 4);

    // The file shrank outside ned between "runs" -- the restore must clamp,
    // not land past the end or throw.
    WriteTestFile(file, "x\n");
    ned::text::Buffer restored = ned::text::Buffer::FromFile(file);
    RestoreFilePlace(restored, 4);
    REQUIRE(restored.Point() <= restored.Content().ByteLength());
}

TEST_CASE("Save-place disabled turns record, restore, and lookup into no-ops", "[Session]") {
    SessionStateGuard           guard;
    const std::filesystem::path dir  = FreshTestDir("ned_session_test_disabled");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "alpha\nbravo\n");

    REQUIRE(SavePlaceEnabled());

    ned::text::Buffer recorded = ned::text::Buffer::FromFile(file);
    recorded.SetPoint(recorded.ByteOffsetForLineAndColumn(1, 0));
    RecordFilePlace(recorded, std::nullopt, 4);

    SetSavePlaceEnabled(false);
    REQUIRE_FALSE(SavePlaceEnabled());
    REQUIRE_FALSE(StoredFilePlaceFor(recorded).has_value());

    ned::text::Buffer restored = ned::text::Buffer::FromFile(file);
    RestoreFilePlace(restored, 4);
    REQUIRE(restored.Point() == 0);

    // Recording while disabled must not write either.
    RecordFilePlace(recorded, 5, 4);
    SetSavePlaceEnabled(true);
    REQUIRE(StoredFilePlaceFor(recorded)->topLine == std::nullopt);
}

TEST_CASE("A pathless buffer never records or restores", "[Session]") {
    SessionStateGuard guard;

    ned::text::Buffer scratch("scratch");
    scratch.InsertAtPoint("some text");
    RecordFilePlace(scratch, std::nullopt, 4);
    REQUIRE_FALSE(StoredFilePlaceFor(scratch).has_value());
    RestoreFilePlace(scratch, 4); // must be a no-op, not a throw
}
