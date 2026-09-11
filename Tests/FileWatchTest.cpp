#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "Editor/FileWatch.h"

using namespace ned::editor;

namespace {

// FileWatchEnabled is process-wide state (see FileWatch.h); every test that
// flips it must leave it default-on for the next test, guaranteed via RAII
// -- the AutoRevertGuard pattern.
struct FileWatchGuard {
    ~FileWatchGuard() {
        SetFileWatchEnabled(true);
    }
};

// A fresh directory per test so events from one test's files can never leak
// into another's watch set.
std::filesystem::path MakeTempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteFile(const std::filesystem::path& path, const char* content) {
    std::ofstream(path, std::ios::trunc) << content;
}

// Generous 2s deadline (the watcher's debounce quiet window is 100ms) --
// the ManagerTest WaitUntil shape, minus the EventLoop (FileWatcher's
// callback fires on its own thread, nothing to drain).
template <typename Predicate>
bool WaitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

// Bounded negative wait: long enough to outlast the debounce cap, so "no
// callback arrived" is a meaningful assertion, not a race won.
void SettleNegative() {
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
}

} // namespace

TEST_CASE("FileWatcher is active on a normal system", "[FileWatch]") {
    FileWatcher watcher([] {});
    REQUIRE(watcher.Active());
}

TEST_CASE("Direct write to a watched file fires the callback", "[FileWatch]") {
    const std::filesystem::path dir  = MakeTempDir("ned_filewatch_direct");
    const std::filesystem::path file = dir / "watched.txt";
    WriteFile(file, "original\n");

    std::atomic<int> fired{0};
    FileWatcher      watcher([&fired] { ++fired; });
    REQUIRE(watcher.Active());
    watcher.SetWatchedFiles({file});

    std::jthread writer([&file] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        WriteFile(file, "changed outside\n");
    });

    REQUIRE(WaitFor([&fired] { return fired.load() >= 1; }));

    std::filesystem::remove_all(dir);
}

TEST_CASE("Rename-replace of a watched file fires the callback", "[FileWatch]") {
    const std::filesystem::path dir  = MakeTempDir("ned_filewatch_rename");
    const std::filesystem::path file = dir / "watched.txt";
    WriteFile(file, "original\n");

    std::atomic<int> fired{0};
    FileWatcher      watcher([&fired] { ++fired; });
    watcher.SetWatchedFiles({file});

    // ProjectReplace's exact save shape: write a sibling temp file, then
    // rename it over the watched name (invisible to a file-level watch --
    // the whole reason the watcher watches parent directories).
    std::jthread writer([&dir, &file] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const std::filesystem::path temp = dir / "watched.txt.ned-tmp";
        WriteFile(temp, "replaced via rename\n");
        std::filesystem::rename(temp, file);
    });

    REQUIRE(WaitFor([&fired] { return fired.load() >= 1; }));

    std::filesystem::remove_all(dir);
}

TEST_CASE("An unrelated sibling file does not fire the callback", "[FileWatch]") {
    const std::filesystem::path dir  = MakeTempDir("ned_filewatch_sibling");
    const std::filesystem::path file = dir / "watched.txt";
    WriteFile(file, "original\n");

    std::atomic<int> fired{0};
    FileWatcher      watcher([&fired] { ++fired; });
    watcher.SetWatchedFiles({file});

    WriteFile(dir / "unrelated.txt", "sibling churn\n");
    SettleNegative();
    REQUIRE(fired.load() == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("SetWatchedFiles resync drops directories no longer watched", "[FileWatch]") {
    const std::filesystem::path dirA  = MakeTempDir("ned_filewatch_resync_a");
    const std::filesystem::path dirB  = MakeTempDir("ned_filewatch_resync_b");
    const std::filesystem::path fileA = dirA / "a.txt";
    const std::filesystem::path fileB = dirB / "b.txt";
    WriteFile(fileA, "a\n");
    WriteFile(fileB, "b\n");

    std::atomic<int> fired{0};
    FileWatcher      watcher([&fired] { ++fired; });
    watcher.SetWatchedFiles({fileA});
    watcher.SetWatchedFiles({fileB});

    WriteFile(fileA, "a changed\n");
    SettleNegative();
    REQUIRE(fired.load() == 0);

    WriteFile(fileB, "b changed\n");
    REQUIRE(WaitFor([&fired] { return fired.load() >= 1; }));

    std::filesystem::remove_all(dirA);
    std::filesystem::remove_all(dirB);
}

TEST_CASE("A burst of rapid writes coalesces into one callback", "[FileWatch]") {
    const std::filesystem::path dir  = MakeTempDir("ned_filewatch_burst");
    const std::filesystem::path file = dir / "watched.txt";
    WriteFile(file, "original\n");

    std::atomic<int> fired{0};
    FileWatcher      watcher([&fired] { ++fired; });
    watcher.SetWatchedFiles({file});

    for (int i = 0; i < 5; ++i) {
        WriteFile(file, "burst write\n");
    }
    REQUIRE(WaitFor([&fired] { return fired.load() >= 1; }));
    SettleNegative();
    REQUIRE(fired.load() == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Destruction does not hang", "[FileWatch]") {
    const std::filesystem::path dir  = MakeTempDir("ned_filewatch_shutdown");
    const std::filesystem::path file = dir / "watched.txt";
    WriteFile(file, "original\n");
    {
        FileWatcher watcher([] {});
        watcher.SetWatchedFiles({file});
        // Nothing asserted -- prompt scope exit IS the test (the
        // PtyProcessTest shutdown-shape).
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("A file under a nonexistent directory is tolerated", "[FileWatch]") {
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "ned_filewatch_missing_dir" / "no_such.txt";
    std::filesystem::remove_all(missing.parent_path());

    FileWatcher watcher([] {});
    watcher.SetWatchedFiles({missing}); // add_watch fails; must not throw
    REQUIRE(watcher.Active());
}

TEST_CASE("SetFileWatchEnabled round-trips and defaults on", "[FileWatch]") {
    const FileWatchGuard guard;
    REQUIRE(FileWatchEnabled());
    SetFileWatchEnabled(false);
    REQUIRE_FALSE(FileWatchEnabled());
}

//
// file-rename-propagation follow-up: a rename reported AS a rename. Without
// the cookie pairing below, all of these look identical to an unrelated
// delete plus create.
//

TEST_CASE("Renaming a watched file in place reports the move", "[FileWatch]") {
    const std::filesystem::path dir  = MakeTempDir("ned_filewatch_move_same_dir");
    const std::filesystem::path file = dir / "old.txt";
    WriteFile(file, "content\n");

    std::mutex            movesMutex;
    std::vector<FileMove> moves;
    FileWatcher           watcher([] {}, [&](std::vector<FileMove> reported) {
        const std::lock_guard lock(movesMutex);
        moves.insert(moves.end(), reported.begin(), reported.end()); });
    REQUIRE(watcher.Active());
    watcher.SetWatchedFiles({file});

    std::filesystem::rename(file, dir / "new.txt");

    REQUIRE(WaitFor([&] {
        const std::lock_guard lock(movesMutex);
        return !moves.empty();
    }));
    const std::lock_guard lock(movesMutex);
    REQUIRE(moves.size() == 1);
    CHECK(moves[0].from == file);
    CHECK(moves[0].to == dir / "new.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("A move between two watched directories is paired across them", "[FileWatch]") {
    const std::filesystem::path dir = MakeTempDir("ned_filewatch_move_cross_dir");
    std::filesystem::create_directories(dir / "a");
    std::filesystem::create_directories(dir / "b");
    const std::filesystem::path file  = dir / "a" / "moved.txt";
    const std::filesystem::path other = dir / "b" / "open.txt";
    WriteFile(file, "content\n");
    WriteFile(other, "content\n");

    std::mutex            movesMutex;
    std::vector<FileMove> moves;
    FileWatcher           watcher([] {}, [&](std::vector<FileMove> reported) {
        const std::lock_guard lock(movesMutex);
        moves.insert(moves.end(), reported.begin(), reported.end()); });
    // Both directories are watched only because a file is open in each --
    // exactly the condition the header documents as the limit of what can
    // be paired.
    watcher.SetWatchedFiles({file, other});

    std::filesystem::rename(file, dir / "b" / "moved.txt");

    REQUIRE(WaitFor([&] {
        const std::lock_guard lock(movesMutex);
        return !moves.empty();
    }));
    const std::lock_guard lock(movesMutex);
    REQUIRE(moves.size() == 1);
    CHECK(moves[0].from == file);
    CHECK(moves[0].to == dir / "b" / "moved.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("A move into an unwatched directory still fires the change callback", "[FileWatch]") {
    const std::filesystem::path dir = MakeTempDir("ned_filewatch_move_unwatched");
    std::filesystem::create_directories(dir / "a");
    std::filesystem::create_directories(dir / "elsewhere");
    const std::filesystem::path file = dir / "a" / "moved.txt";
    WriteFile(file, "content\n");

    std::atomic<int>      changes{0};
    std::mutex            movesMutex;
    std::vector<FileMove> moves;
    FileWatcher           watcher([&] { ++changes; }, [&](std::vector<FileMove> reported) {
        const std::lock_guard lock(movesMutex);
        moves.insert(moves.end(), reported.begin(), reported.end()); });
    watcher.SetWatchedFiles({file});

    std::filesystem::rename(file, dir / "elsewhere" / "moved.txt");

    // The file under the buffer did change, so the sweep still runs; there
    // is simply no destination to name, and none is guessed at.
    REQUIRE(WaitFor([&] { return changes.load() > 0; }));
    const std::lock_guard lock(movesMutex);
    CHECK(moves.empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("A move of an unopened file in a watched directory is reported too", "[FileWatch]") {
    // The file worth following is routinely one no buffer has open (a git
    // mv of a header nobody is editing); the directory is watched either
    // way, so the events arrive either way. Deciding which of these is
    // meaningful is the consumer's job -- WindowManagerTest pins that half.
    const std::filesystem::path dir   = MakeTempDir("ned_filewatch_move_unopened");
    const std::filesystem::path open  = dir / "open.txt";
    const std::filesystem::path other = dir / "other.txt";
    WriteFile(open, "content\n");
    WriteFile(other, "content\n");

    std::mutex            movesMutex;
    std::vector<FileMove> moves;
    FileWatcher           watcher([] {}, [&](std::vector<FileMove> reported) {
        const std::lock_guard lock(movesMutex);
        moves.insert(moves.end(), reported.begin(), reported.end()); });
    watcher.SetWatchedFiles({open}); // only open.txt has a buffer

    std::filesystem::rename(other, dir / "renamed.txt");

    REQUIRE(WaitFor([&] {
        const std::lock_guard lock(movesMutex);
        return !moves.empty();
    }));
    const std::lock_guard lock(movesMutex);
    REQUIRE(moves.size() == 1);
    CHECK(moves[0].from == other);
    CHECK(moves[0].to == dir / "renamed.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Churn in a watched directory that is not a rename reports no move", "[FileWatch]") {
    const std::filesystem::path dir  = MakeTempDir("ned_filewatch_move_unrelated");
    const std::filesystem::path file = dir / "watched.txt";
    WriteFile(file, "content\n");

    std::mutex            movesMutex;
    std::vector<FileMove> moves;
    FileWatcher           watcher([] {}, [&](std::vector<FileMove> reported) {
        const std::lock_guard lock(movesMutex);
        moves.insert(moves.end(), reported.begin(), reported.end()); });
    watcher.SetWatchedFiles({file});

    WriteFile(dir / "sibling.txt", "content\n");
    std::filesystem::remove(dir / "sibling.txt");
    WriteFile(file, "changed\n");

    SettleNegative();
    const std::lock_guard lock(movesMutex);
    CHECK(moves.empty());

    std::filesystem::remove_all(dir);
}
