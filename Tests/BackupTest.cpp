#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/Backup.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::AutoSaveFileBuffers;
using ned::editor::BackupDirectoryForFile;
using ned::editor::BackupFileBeforeSave;
using ned::editor::BackupMaxAgeDays;
using ned::editor::BackupMaxSizeMb;
using ned::editor::BackupMaxVersions;
using ned::editor::BackupsDirectory;
using ned::editor::BackupVersion;
using ned::editor::FileAutoSaveEnabled;
using ned::editor::ListBackupVersions;
using ned::editor::MaybePruneBackups;
using ned::editor::PruneBackups;
using ned::editor::ReadBackupVersion;
using ned::editor::RemoveAutoSave;
using ned::editor::ResetBackupsForTesting;
using ned::editor::SetBackupMaxAgeDays;
using ned::editor::SetBackupMaxSizeMb;
using ned::editor::SetBackupMaxVersions;
using ned::editor::SetFileAutoSaveEnabled;
using ned::editor::WriteAutoSave;

namespace {

// Mirrors InitFileTest.cpp's own EnvVarGuard exactly -- saves/restores an
// environment variable's previous state (including "was unset") around a
// test, so these tests don't leak XDG_STATE_HOME/HOME overrides into anything
// else running in this process.
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

// Backup settings and the auto-save generation memo are process-wide state;
// every test resets them on the way out (ScratchPadTest.cpp's AutoSaveGuard
// precedent, widened to the whole module the way ResetBackupsForTesting is).
struct BackupSettingsGuard {
    BackupSettingsGuard() {
        ResetBackupsForTesting();
    }
    ~BackupSettingsGuard() {
        ResetBackupsForTesting();
    }
};

// One disposable sandbox per test: a temp root serving as XDG_STATE_HOME (so
// backups land inside it), XDG_DATA_HOME (so the scratch-skip check resolves
// inside it too), and a work/ directory for the files being "edited".
struct BackupSandbox {
    explicit BackupSandbox(const std::string& name) : root(std::filesystem::temp_directory_path() / name), state(root / "state"), data(root / "data"),
                                                      work(root / "work"), stateGuard("XDG_STATE_HOME", state.c_str()), dataGuard("XDG_DATA_HOME", data.c_str()),
                                                      homeGuard("HOME", nullptr) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(work);
    }

    ~BackupSandbox() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path WriteWorkFile(const std::string& name, const std::string& content) const {
        const std::filesystem::path path = work / name;
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
        return path;
    }

    std::filesystem::path root;
    std::filesystem::path state;
    std::filesystem::path data;
    std::filesystem::path work;
    BackupSettingsGuard   settingsGuard;
    EnvVarGuard           stateGuard;
    EnvVarGuard           dataGuard;
    EnvVarGuard           homeGuard;
};

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::size_t CountVersionFiles(const std::filesystem::path& directory) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("v-") && name.ends_with(".bak")) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("BackupsDirectory prefers XDG_STATE_HOME when set", "[Backup]") {
    EnvVarGuard state("XDG_STATE_HOME", "/tmp/ned-backup-test-state");
    EnvVarGuard home("HOME", "/tmp/ned-backup-test-home");

    REQUIRE(BackupsDirectory() == std::filesystem::path("/tmp/ned-backup-test-state/ned/backups"));
}

TEST_CASE("BackupsDirectory falls back to HOME/.local/state when XDG_STATE_HOME is unset", "[Backup]") {
    EnvVarGuard state("XDG_STATE_HOME", nullptr);
    EnvVarGuard home("HOME", "/tmp/ned-backup-test-home");

    REQUIRE(BackupsDirectory() == std::filesystem::path("/tmp/ned-backup-test-home/.local/state/ned/backups"));
}

TEST_CASE("BackupsDirectory throws when neither XDG_STATE_HOME nor HOME is set", "[Backup]") {
    EnvVarGuard state("XDG_STATE_HOME", nullptr);
    EnvVarGuard home("HOME", nullptr);

    REQUIRE_THROWS_AS(BackupsDirectory(), std::runtime_error);
}

TEST_CASE("BackupDirectoryForFile is stable per file and distinct across files", "[Backup]") {
    EnvVarGuard state("XDG_STATE_HOME", "/tmp/ned-backup-test-state");
    EnvVarGuard home("HOME", nullptr);

    const std::filesystem::path a = BackupDirectoryForFile("/tmp/ned-backup-test/a.txt");
    REQUIRE(a == BackupDirectoryForFile("/tmp/ned-backup-test/a.txt"));
    REQUIRE(a.parent_path() == BackupsDirectory());
    REQUIRE(a != BackupDirectoryForFile("/tmp/ned-backup-test/b.txt"));
}

TEST_CASE("BackupFileBeforeSave preserves the prior disk content as a timestamped version", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_before_save");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "prior content");

    BackupFileBeforeSave(file, 1755700000); // 2025-08-20T14:26:40Z

    const std::filesystem::path directory = BackupDirectoryForFile(file);
    const std::filesystem::path version   = directory / "v-20250820-142640-00.bak";
    REQUIRE(std::filesystem::exists(version));
    REQUIRE(ReadFile(version) == "prior content");

    // The path sidecar maps the hash-named directory back to its file.
    const std::string sidecar = ReadFile(directory / "path");
    REQUIRE(sidecar.find("notes.txt") != std::string::npos);
    REQUIRE(sidecar.back() == '\n');
}

TEST_CASE("BackupFileBeforeSave sequences same-second versions -00, -01, ...", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_sequence");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "first");

    BackupFileBeforeSave(file, 1755700000);
    sandbox.WriteWorkFile("notes.txt", "second");
    BackupFileBeforeSave(file, 1755700000);

    const std::filesystem::path directory = BackupDirectoryForFile(file);
    REQUIRE(ReadFile(directory / "v-20250820-142640-00.bak") == "first");
    REQUIRE(ReadFile(directory / "v-20250820-142640-01.bak") == "second");
}

TEST_CASE("BackupFileBeforeSave is a no-op for a file that doesn't exist yet", "[Backup]") {
    const BackupSandbox sandbox("ned_backup_test_missing");

    BackupFileBeforeSave(sandbox.work / "never-written.txt", 1755700000);

    REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(sandbox.work / "never-written.txt")));
}

TEST_CASE("BackupFileBeforeSave is a no-op for a file directly inside the scratch directory", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_scratch");
    const std::filesystem::path scratchDir = sandbox.data / "ned" / "scratches";
    std::filesystem::create_directories(scratchDir);
    const std::filesystem::path scratch = scratchDir / "todo.txt";
    std::ofstream(scratch) << "scratch content";

    BackupFileBeforeSave(scratch, 1755700000);

    REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(scratch)));
}

TEST_CASE("BackupFileBeforeSave is a no-op for an oversized file", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_oversize");
    const std::filesystem::path file = sandbox.WriteWorkFile("huge.bin", "x");
    std::filesystem::resize_file(file, 65ull * 1024 * 1024); // sparse -- past the default 64 MiB cutoff

    BackupFileBeforeSave(file, 1755700000);

    REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(file)));
}

TEST_CASE("BackupFileBeforeSave honors a configured max-size cutoff", "[Backup]") {
    const BackupSettingsGuard   guard;
    const BackupSandbox         sandbox("ned_backup_test_configured_max_size");
    const std::filesystem::path file = sandbox.WriteWorkFile("small.bin", "x");
    std::filesystem::resize_file(file, 2ull * 1024 * 1024); // 2 MiB -- well under the default cutoff

    SetBackupMaxSizeMb(1); // now past the configured cutoff
    BackupFileBeforeSave(file, 1755700000);
    REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(file)));

    SetBackupMaxSizeMb(4); // back under the cutoff
    BackupFileBeforeSave(file, 1755700000);
    REQUIRE(std::filesystem::exists(BackupDirectoryForFile(file)));
}

TEST_CASE("ListBackupVersions returns an empty list when nothing was ever backed up", "[Backup]") {
    const BackupSandbox sandbox("ned_backup_test_list_empty");

    REQUIRE(ListBackupVersions(sandbox.work / "notes.txt").empty());
}

TEST_CASE("ListBackupVersions orders the autosave first, then versions newest-first across a day boundary",
          "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_list_order");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "old");

    BackupFileBeforeSave(file, 1755734399); // 2025-08-20T23:59:59Z
    sandbox.WriteWorkFile("notes.txt", "new");
    BackupFileBeforeSave(file, 1755734401); // 2025-08-21T00:00:01Z
    WriteAutoSave(file, "unsaved edits");

    const std::vector<BackupVersion> versions = ListBackupVersions(file);
    REQUIRE(versions.size() == 3);
    REQUIRE(versions[0].isAutoSave);
    REQUIRE(versions[0].label == "autosave (crash recovery)");
    REQUIRE(ReadBackupVersion(versions[0].path) == "unsaved edits");
    REQUIRE_FALSE(versions[1].isAutoSave);
    REQUIRE(ReadBackupVersion(versions[1].path) == "new");
    REQUIRE(ReadBackupVersion(versions[2].path) == "old");
    REQUIRE(versions[1].timestampSeconds > versions[2].timestampSeconds);
    REQUIRE_FALSE(versions[1].label.empty());
}

TEST_CASE("ReadBackupVersion throws for a missing version file", "[Backup]") {
    const BackupSandbox sandbox("ned_backup_test_read_missing");

    REQUIRE_THROWS_AS(ReadBackupVersion(sandbox.root / "no-such-file"), std::runtime_error);
}

TEST_CASE("WriteAutoSave and RemoveAutoSave round-trip the autosave snapshot", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_autosave_roundtrip");
    const std::filesystem::path file = sandbox.work / "notes.txt"; // needn't exist on disk

    WriteAutoSave(file, "crash snapshot");
    const std::filesystem::path autosave = BackupDirectoryForFile(file) / "autosave";
    REQUIRE(ReadFile(autosave) == "crash snapshot");

    RemoveAutoSave(file);
    REQUIRE_FALSE(std::filesystem::exists(autosave));
}

TEST_CASE("AutoSaveFileBuffers writes an autosave for a modified file buffer", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_tick_basic");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "on disk");

    ned::text::BufferList bufferList;
    ned::text::Buffer&    buffer = bufferList.OpenOrCreateFile(file);
    buffer.InsertAtPoint("unsaved ");

    AutoSaveFileBuffers(bufferList);

    REQUIRE(ReadFile(BackupDirectoryForFile(file) / "autosave") == "unsaved on disk");
    REQUIRE(buffer.Modified()); // an autosave is a copy, never a save of the buffer itself
    REQUIRE(ReadFile(file) == "on disk");
}

TEST_CASE("AutoSaveFileBuffers skips buffers that must not be snapshotted", "[Backup]") {
    const BackupSandbox sandbox("ned_backup_test_tick_skips");

    ned::text::BufferList bufferList;

    SECTION("disabled via SetFileAutoSaveEnabled") {
        const std::filesystem::path file   = sandbox.WriteWorkFile("notes.txt", "on disk");
        ned::text::Buffer&          buffer = bufferList.OpenOrCreateFile(file);
        buffer.InsertAtPoint("edit");
        SetFileAutoSaveEnabled(false);

        AutoSaveFileBuffers(bufferList);

        REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(file)));
    }

    SECTION("pathless buffer") {
        ned::text::Buffer& buffer = bufferList.CreateBuffer("scratch");
        buffer.InsertAtPoint("edit");

        AutoSaveFileBuffers(bufferList); // must not throw

        REQUIRE_FALSE(std::filesystem::exists(BackupsDirectory()));
    }

    SECTION("unmodified buffer") {
        const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "on disk");
        bufferList.OpenOrCreateFile(file);

        AutoSaveFileBuffers(bufferList);

        REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(file)));
    }

    SECTION("scratch-directory buffer") {
        const std::filesystem::path scratchDir = sandbox.data / "ned" / "scratches";
        std::filesystem::create_directories(scratchDir);
        std::ofstream(scratchDir / "todo.txt") << "scratch";
        ned::text::Buffer& buffer = bufferList.OpenOrCreateFile(scratchDir / "todo.txt");
        buffer.InsertAtPoint("edit");

        AutoSaveFileBuffers(bufferList);

        REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(scratchDir / "todo.txt")));
    }

    SECTION("unmodified preview buffer -- and an edited one is promoted, so it does snapshot") {
        const std::filesystem::path file   = sandbox.WriteWorkFile("notes.txt", "on disk");
        ned::text::Buffer&          buffer = bufferList.OpenOrCreateFile(file);
        bufferList.SetPreviewBuffer(&buffer);

        AutoSaveFileBuffers(bufferList);
        REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(file)));

        buffer.InsertAtPoint("edit "); // PreviewBuffer() self-promotes on modification
        AutoSaveFileBuffers(bufferList);
        REQUIRE(std::filesystem::exists(BackupDirectoryForFile(file) / "autosave"));
    }
}

TEST_CASE("AutoSaveFileBuffers skips rewriting an unchanged snapshot via the generation memo", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_tick_memo");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "on disk");

    ned::text::BufferList bufferList;
    ned::text::Buffer&    buffer = bufferList.OpenOrCreateFile(file);
    buffer.InsertAtPoint("edit ");
    AutoSaveFileBuffers(bufferList);

    const std::filesystem::path autosave = BackupDirectoryForFile(file) / "autosave";
    REQUIRE(std::filesystem::exists(autosave));

    // Content unchanged since the last tick: even a deleted snapshot isn't
    // rewritten (the memo, not the file's existence, is what's consulted).
    std::filesystem::remove(autosave);
    AutoSaveFileBuffers(bufferList);
    REQUIRE_FALSE(std::filesystem::exists(autosave));

    // A real edit bumps ContentGeneration and earns a fresh snapshot.
    buffer.InsertAtPoint("more ");
    AutoSaveFileBuffers(bufferList);
    REQUIRE(ReadFile(autosave) == "edit more on disk");
}

TEST_CASE("RemoveAutoSave clears the generation memo so the next tick rewrites", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_memo_clear");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "on disk");

    ned::text::BufferList bufferList;
    ned::text::Buffer&    buffer = bufferList.OpenOrCreateFile(file);
    buffer.InsertAtPoint("edit ");
    AutoSaveFileBuffers(bufferList);

    const std::filesystem::path autosave = BackupDirectoryForFile(file) / "autosave";
    RemoveAutoSave(file);
    REQUIRE_FALSE(std::filesystem::exists(autosave));

    AutoSaveFileBuffers(bufferList); // same generation, but the memo was forgotten
    REQUIRE(ReadFile(autosave) == "edit on disk");
}

TEST_CASE("PruneBackups deletes versions past the age limit and keeps newer ones", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_prune_age");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "content");

    const std::int64_t now    = 1755700000;
    const std::int64_t cutoff = now - 14ll * 24 * 60 * 60;
    BackupFileBeforeSave(file, cutoff - 1); // one second too old
    BackupFileBeforeSave(file, cutoff + 1); // one second inside the window

    PruneBackups(now);

    const std::filesystem::path directory = BackupDirectoryForFile(file);
    REQUIRE(CountVersionFiles(directory) == 1);
    const std::vector<BackupVersion> versions = ListBackupVersions(file);
    REQUIRE(versions.size() == 1);
    REQUIRE(versions[0].timestampSeconds == cutoff + 1);
}

TEST_CASE("PruneBackups caps the version count, evicting oldest first", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_prune_cap");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "content");

    SetBackupMaxVersions(2);
    for (int i = 0; i < 4; ++i) {
        BackupFileBeforeSave(file, 1755700000 + i);
    }

    PruneBackups(1755700010);

    const std::vector<BackupVersion> versions = ListBackupVersions(file);
    REQUIRE(versions.size() == 2);
    REQUIRE(versions[0].timestampSeconds == 1755700003);
    REQUIRE(versions[1].timestampSeconds == 1755700002);
}

TEST_CASE("PruneBackups deletes an orphaned autosave past the age limit", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_prune_orphan");
    const std::filesystem::path file = sandbox.work / "notes.txt";

    WriteAutoSave(file, "orphaned");
    const std::filesystem::path autosave = BackupDirectoryForFile(file) / "autosave";
    // Age the snapshot on disk: prune judges an autosave by its mtime.
    std::filesystem::last_write_time(autosave,
                                     std::filesystem::file_time_type::clock::now() - std::chrono::hours(15 * 24));

    PruneBackups();

    REQUIRE_FALSE(std::filesystem::exists(autosave));
    // Nothing recoverable left, so the whole per-file directory is gone too.
    REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(file)));
}

TEST_CASE("PruneBackups removes a directory left holding only its path sidecar", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_prune_sidecar_only");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "content");

    BackupFileBeforeSave(file, 1755700000);
    SetBackupMaxAgeDays(1);

    PruneBackups(1755700000 + 2ll * 24 * 60 * 60);

    REQUIRE_FALSE(std::filesystem::exists(BackupDirectoryForFile(file)));
}

TEST_CASE("PruneBackups with non-positive limits disables that dimension", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_prune_disabled");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "content");

    SetBackupMaxAgeDays(0);
    SetBackupMaxVersions(0);
    for (int i = 0; i < 3; ++i) {
        BackupFileBeforeSave(file, 1755700000 + i);
    }

    PruneBackups(1755700000 + 100ll * 24 * 60 * 60); // far future -- everything would age out

    REQUIRE(ListBackupVersions(file).size() == 3);
}

TEST_CASE("MaybePruneBackups runs at most once per hour", "[Backup]") {
    const BackupSandbox         sandbox("ned_backup_test_maybe_prune");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "content");

    MaybePruneBackups(1755700000); // stamps the last-run time

    // A version that is already stale relative to "now": a real prune would
    // delete it, so its survival proves the rate limiter skipped.
    SetBackupMaxAgeDays(1);
    BackupFileBeforeSave(file, 1755500000);

    MaybePruneBackups(1755700000 + 60); // within the hour -- skipped
    REQUIRE(ListBackupVersions(file).size() == 1);

    MaybePruneBackups(1755700000 + 3601); // past the hour -- prunes
    REQUIRE(ListBackupVersions(file).empty());
}

TEST_CASE("Backup settings default and round-trip", "[Backup]") {
    const BackupSettingsGuard guard;

    REQUIRE(FileAutoSaveEnabled());
    REQUIRE(BackupMaxAgeDays() == 14);
    REQUIRE(BackupMaxVersions() == 20);
    REQUIRE(BackupMaxSizeMb() == 64);

    SetFileAutoSaveEnabled(false);
    SetBackupMaxAgeDays(7);
    SetBackupMaxVersions(5);
    SetBackupMaxSizeMb(16);
    REQUIRE_FALSE(FileAutoSaveEnabled());
    REQUIRE(BackupMaxAgeDays() == 7);
    REQUIRE(BackupMaxVersions() == 5);
    REQUIRE(BackupMaxSizeMb() == 16);

    SetBackupMaxSizeMb(-3); // clamped to 1, same "don't throw, just make it sane" convention as SetTabWidth
    REQUIRE(BackupMaxSizeMb() == 1);

    ResetBackupsForTesting();
    REQUIRE(FileAutoSaveEnabled());
    REQUIRE(BackupMaxAgeDays() == 14);
    REQUIRE(BackupMaxVersions() == 20);
    REQUIRE(BackupMaxSizeMb() == 64);
}
