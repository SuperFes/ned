#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Bookmark.h"
#include "Editor/RecentFiles.h"
#include "Text/Buffer.h"

using ned::editor::Bookmark;
using ned::editor::BookmarkNames;
using ned::editor::BookmarksPath;
using ned::editor::BookmarkStore;
using ned::editor::DeleteBookmark;
using ned::editor::FindBookmark;
using ned::editor::RecentFilesStore;
using ned::editor::RecordBookmark;
using ned::editor::ResetBookmarksForTesting;

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

struct BookmarkStateGuard {
    BookmarkStateGuard() {
        ResetBookmarksForTesting();
    }
    ~BookmarkStateGuard() {
        ResetBookmarksForTesting();
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

TEST_CASE("BookmarkStore sets and finds by name", "[Bookmark]") {
    BookmarkStore store;
    REQUIRE_FALSE(store.Find("here").has_value());

    store.Set(Bookmark{.name = "here", .path = "/tmp/a.txt", .line = 3, .column = 1});

    const auto mark = store.Find("here");
    REQUIRE(mark.has_value());
    REQUIRE(mark->path == "/tmp/a.txt");
    REQUIRE(mark->line == 3);
    REQUIRE(mark->column == 1);
    REQUIRE(store.Count() == 1);
}

TEST_CASE("BookmarkStore Set overwrites an existing name rather than duplicating", "[Bookmark]") {
    BookmarkStore store;
    store.Set(Bookmark{.name = "here", .path = "/tmp/a.txt", .line = 1, .column = 0});
    store.Set(Bookmark{.name = "here", .path = "/tmp/b.txt", .line = 2, .column = 0});

    REQUIRE(store.Count() == 1);
    REQUIRE(store.Find("here")->path == "/tmp/b.txt");
}

TEST_CASE("BookmarkStore deletes by name", "[Bookmark]") {
    BookmarkStore store;
    store.Set(Bookmark{.name = "here", .path = "/tmp/a.txt", .line = 0, .column = 0});

    REQUIRE(store.Delete("here"));
    REQUIRE_FALSE(store.Find("here").has_value());
    REQUIRE_FALSE(store.Delete("here")); // already gone -- false, not a throw
}

TEST_CASE("BookmarkStore Names is sorted alphabetically", "[Bookmark]") {
    BookmarkStore store;
    store.Set(Bookmark{.name = "zebra", .path = "/tmp/z.txt"});
    store.Set(Bookmark{.name = "apple", .path = "/tmp/a.txt"});
    store.Set(Bookmark{.name = "mango", .path = "/tmp/m.txt"});

    REQUIRE(store.Names() == std::vector<std::string>{"apple", "mango", "zebra"});
}

TEST_CASE("BookmarkStore JSON round-trips", "[Bookmark]") {
    BookmarkStore store;
    store.Set(Bookmark{.name = "here", .path = "/tmp/a.txt", .line = 4, .column = 2});

    const BookmarkStore loaded = BookmarkStore::FromJson(store.ToJson());
    REQUIRE(loaded.Count() == 1);
    REQUIRE(loaded.Find("here") == Bookmark{.name = "here", .path = "/tmp/a.txt", .line = 4, .column = 2});
    REQUIRE_FALSE(loaded.Dirty());
}

TEST_CASE("BookmarkStore tolerates malformed JSON and malformed entries", "[Bookmark]") {
    REQUIRE(BookmarkStore::FromJson("not json at all").Count() == 0);
    REQUIRE(BookmarkStore::FromJson("{}").Count() == 0);
    // One entry missing its path must not discard the valid one beside it.
    const BookmarkStore store = BookmarkStore::FromJson(
        R"({"version":1,"bookmarks":[{"name":"broken"},{"name":"ok","path":"/tmp/x","line":1,"column":2}]})");
    REQUIRE(store.Count() == 1);
    REQUIRE(store.Find("ok").has_value());
}

TEST_CASE("BookmarkStore saves to and loads from a file", "[Bookmark]") {
    const std::filesystem::path dir       = FreshTestDir("ned_bookmark_test_file");
    const std::filesystem::path storePath = dir / "state" / "bookmarks.json"; // parent must be created

    BookmarkStore store;
    store.Set(Bookmark{.name = "here", .path = "/tmp/a.txt", .line = 1, .column = 0});
    store.SaveToFile(storePath);

    BookmarkStore loaded;
    loaded.LoadFromFile(storePath);
    REQUIRE(loaded.Count() == 1);

    loaded.LoadFromFile(dir / "does-not-exist.json");
    REQUIRE(loaded.Count() == 0);
}

TEST_CASE("BookmarksPath prefers XDG_STATE_HOME, falls back to HOME", "[Bookmark]") {
    {
        EnvVarGuard xdg("XDG_STATE_HOME", "/tmp/ned-xdg-test-state");
        EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");
        REQUIRE(BookmarksPath() == std::filesystem::path("/tmp/ned-xdg-test-state/ned/bookmarks.json"));
    }
    {
        EnvVarGuard xdg("XDG_STATE_HOME", nullptr);
        EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");
        REQUIRE(BookmarksPath() == std::filesystem::path("/tmp/ned-xdg-test-home/.local/state/ned/bookmarks.json"));
    }
}

TEST_CASE("RecordBookmark and FindBookmark round-trip through a Buffer", "[Bookmark]") {
    BookmarkStateGuard          guard;
    const std::filesystem::path dir  = FreshTestDir("ned_bookmark_test_buffer");
    const std::filesystem::path file = WriteTestFile(dir / "a.txt", "alpha\nbravo\ncharlie\n");

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(file);
    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(2, 4)); // "charlie", column 4
    RecordBookmark("mark1", buffer, /*tabWidth=*/4);

    const auto mark = FindBookmark("mark1");
    REQUIRE(mark.has_value());
    REQUIRE(mark->line == 2);
    REQUIRE(mark->column == 4);
    REQUIRE(mark->path == RecentFilesStore::NormalizePathKey(file));

    REQUIRE(BookmarkNames() == std::vector<std::string>{"mark1"});
    REQUIRE(DeleteBookmark("mark1"));
    REQUIRE_FALSE(FindBookmark("mark1").has_value());
}

TEST_CASE("A pathless buffer never records a bookmark", "[Bookmark]") {
    BookmarkStateGuard guard;

    ned::text::Buffer scratch("scratch");
    scratch.InsertAtPoint("some text");
    RecordBookmark("mark1", scratch, 4);
    REQUIRE_FALSE(FindBookmark("mark1").has_value());
    REQUIRE(BookmarkNames().empty());
}
