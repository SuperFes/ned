#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

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
// the LspManagerTest WaitUntil shape, minus the EventLoop (FileWatcher's
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
