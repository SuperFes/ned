#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/PersistentUndo.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::PersistentUndoEnabled;
using ned::editor::PersistentUndoMaxSizeMb;
using ned::editor::ResetPersistentUndoForTesting;
using ned::editor::SaveUndoHistory;
using ned::editor::SaveUndoHistoryForOpenBuffers;
using ned::editor::SetPersistentUndoEnabled;
using ned::editor::SetPersistentUndoMaxSizeMb;
using ned::editor::TryRestoreUndoHistory;
using ned::editor::UndoDirectory;
using ned::editor::UndoFileForPath;

namespace {

// Mirrors BackupTest.cpp's own EnvVarGuard exactly -- saves/restores an
// environment variable's previous state (including "was unset") around a
// test.
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

// Settings + generation memo are process-wide state -- BackupTest.cpp's own
// BackupSettingsGuard precedent.
struct PersistentUndoSettingsGuard {
    PersistentUndoSettingsGuard() {
        ResetPersistentUndoForTesting();
    }
    ~PersistentUndoSettingsGuard() {
        ResetPersistentUndoForTesting();
    }
};

// BackupTest.cpp's own BackupSandbox, trimmed to what this module needs (no
// XDG_DATA_HOME/scratch dir -- ScratchPad's own directory resolution is
// exercised there, not here; the scratch-skip path just needs
// ScratchDirectory() to resolve to *something* that isn't work/).
struct UndoSandbox {
    explicit UndoSandbox(const std::string& name) :
        root(std::filesystem::temp_directory_path() / name), state(root / "state"), work(root / "work"),
        settingsGuard(), stateGuard("XDG_STATE_HOME", state.c_str()), homeGuard("HOME", nullptr) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(work);
    }

    ~UndoSandbox() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path WriteWorkFile(const std::string& name, const std::string& content) const {
        const std::filesystem::path path = work / name;
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
        return path;
    }

    std::filesystem::path       root;
    std::filesystem::path       state;
    std::filesystem::path       work;
    PersistentUndoSettingsGuard settingsGuard;
    EnvVarGuard                 stateGuard;
    EnvVarGuard                 homeGuard;
};

} // namespace

TEST_CASE("UndoDirectory prefers XDG_STATE_HOME when set", "[PersistentUndo]") {
    EnvVarGuard state("XDG_STATE_HOME", "/tmp/ned-undo-test-state");
    EnvVarGuard home("HOME", "/tmp/ned-undo-test-home");
    REQUIRE(UndoDirectory() == std::filesystem::path("/tmp/ned-undo-test-state/ned/undo"));
}

TEST_CASE("UndoFileForPath is stable per file and distinct across files", "[PersistentUndo]") {
    const UndoSandbox sandbox("ned_undo_test_paths");
    const auto        a1 = UndoFileForPath(sandbox.work / "a.txt");
    const auto        a2 = UndoFileForPath(sandbox.work / "a.txt");
    const auto        b  = UndoFileForPath(sandbox.work / "b.txt");
    REQUIRE(a1 == a2);
    REQUIRE(a1 != b);
}

TEST_CASE("TryRestoreUndoHistory restores full history when disk matches the tree's tip", "[PersistentUndo]") {
    const UndoSandbox           sandbox("ned_undo_test_restore_tip");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "a");

    {
        ned::text::BufferList bufferList;
        ned::text::Buffer&    buffer = bufferList.OpenOrCreateFile(file);
        buffer.SetPoint(1);
        buffer.InsertAtPoint("b");
        buffer.MoveBackward();
        buffer.MoveForward();
        buffer.InsertAtPoint("c"); // "abc"
        SaveUndoHistory(buffer);
    }
    // The file is still "a" on disk (buffer was never saved) -- overwrite it
    // to "abc" the way a real save would have, so this reopen's content
    // matches the persisted tree's tip exactly.
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "abc";

    ned::text::BufferList bufferList;
    ned::text::Buffer&    reopened = bufferList.OpenOrCreateFile(file);
    REQUIRE(reopened.Text() == "abc");
    REQUIRE_FALSE(reopened.CanUndo()); // fresh single-node tree until restored

    TryRestoreUndoHistory(reopened);
    REQUIRE(reopened.CanUndo());
    reopened.Undo();
    REQUIRE(reopened.Text() == "ab");
    reopened.Undo();
    REQUIRE(reopened.Text() == "a");
    REQUIRE_FALSE(reopened.CanUndo());
}

TEST_CASE("TryRestoreUndoHistory restores history when disk matches an ancestor, not just the tip",
          "[PersistentUndo]") {
    const UndoSandbox           sandbox("ned_undo_test_restore_ancestor");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "a");

    {
        ned::text::BufferList bufferList;
        ned::text::Buffer&    buffer = bufferList.OpenOrCreateFile(file);
        buffer.SetPoint(1);
        buffer.InsertAtPoint("b");
        buffer.MoveBackward();
        buffer.MoveForward();
        buffer.InsertAtPoint("c"); // tree tip is "abc"; buffer never saved
        SaveUndoHistory(buffer);
    }
    // Disk stays "a" -- the "quit without saving" case: on-disk content
    // matches the tree's *root*, not its tip.

    ned::text::BufferList bufferList;
    ned::text::Buffer&    reopened = bufferList.OpenOrCreateFile(file);
    REQUIRE(reopened.Text() == "a");

    TryRestoreUndoHistory(reopened);
    REQUIRE(reopened.Text() == "a");   // unchanged by the restore itself
    REQUIRE_FALSE(reopened.CanUndo()); // it's the root
    REQUIRE(reopened.CanRedo());       // but the "b"/"abc" branch is still there
    reopened.Redo();
    REQUIRE(reopened.Text() == "ab");
    reopened.Redo();
    REQUIRE(reopened.Text() == "abc");
}

TEST_CASE("TryRestoreUndoHistory leaves a fresh tree alone when no node matches disk content",
          "[PersistentUndo]") {
    const UndoSandbox           sandbox("ned_undo_test_restore_no_match");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "a");

    {
        ned::text::BufferList bufferList;
        ned::text::Buffer&    buffer = bufferList.OpenOrCreateFile(file);
        buffer.InsertAtPoint("b"); // tree: "a", "ab"
        SaveUndoHistory(buffer);
    }
    // An external tool rewrote the file to content the tree never saw.
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "completely different";

    ned::text::BufferList bufferList;
    ned::text::Buffer&    reopened = bufferList.OpenOrCreateFile(file);
    REQUIRE(reopened.Text() == "completely different");

    TryRestoreUndoHistory(reopened);
    REQUIRE_FALSE(reopened.CanUndo()); // discarded, not force-fit
    REQUIRE(reopened.Text() == "completely different");
}

TEST_CASE("SaveUndoHistory is a no-op for buffers that must not be persisted", "[PersistentUndo]") {
    const UndoSandbox sandbox("ned_undo_test_save_skips");

    SECTION("disabled via SetPersistentUndoEnabled") {
        const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "a");
        ned::text::BufferList       bufferList;
        ned::text::Buffer&          buffer = bufferList.OpenOrCreateFile(file);
        buffer.InsertAtPoint("b");
        SetPersistentUndoEnabled(false);

        SaveUndoHistory(buffer);
        REQUIRE_FALSE(std::filesystem::exists(UndoFileForPath(file)));
    }

    SECTION("pathless buffer") {
        ned::text::BufferList bufferList;
        ned::text::Buffer&    buffer = bufferList.CreateBuffer("scratch");
        buffer.InsertAtPoint("b");

        SaveUndoHistory(buffer); // must not throw
    }

    SECTION("no history beyond the loaded root") {
        const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "a");
        ned::text::BufferList       bufferList;
        ned::text::Buffer&          buffer = bufferList.OpenOrCreateFile(file);

        SaveUndoHistory(buffer); // never edited -- single-node tree
        REQUIRE_FALSE(std::filesystem::exists(UndoFileForPath(file)));
    }

    SECTION("content past the configured max-size cutoff") {
        const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "a");
        ned::text::BufferList       bufferList;
        ned::text::Buffer&          buffer = bufferList.OpenOrCreateFile(file);
        SetPersistentUndoMaxSizeMb(1); // 1 MiB cutoff (SetPersistentUndoMaxSizeMb's own minimum)
        buffer.InsertAtPoint(std::string(2 * 1024 * 1024, 'x'));

        SaveUndoHistory(buffer);
        REQUIRE_FALSE(std::filesystem::exists(UndoFileForPath(file)));
    }
}

TEST_CASE("SaveUndoHistoryForOpenBuffers skips rewriting unchanged history via the generation memo",
          "[PersistentUndo]") {
    const UndoSandbox           sandbox("ned_undo_test_memo");
    const std::filesystem::path file = sandbox.WriteWorkFile("notes.txt", "a");

    ned::text::BufferList bufferList;
    ned::text::Buffer&    buffer = bufferList.OpenOrCreateFile(file);
    buffer.InsertAtPoint("b");

    SaveUndoHistoryForOpenBuffers(bufferList);
    const auto firstWrite = std::filesystem::last_write_time(UndoFileForPath(file));

    SaveUndoHistoryForOpenBuffers(bufferList); // nothing changed since -- memo should skip the rewrite
    REQUIRE(std::filesystem::last_write_time(UndoFileForPath(file)) == firstWrite);

    buffer.MoveBackward(); // breaks the coalescing run, see BufferTest.cpp's own "Moving point between inserts..."
    buffer.MoveForward();
    buffer.InsertAtPoint("c");
    SaveUndoHistoryForOpenBuffers(bufferList); // changed -- rewrites
    const auto nodes = buffer.SerializeUndo();
    REQUIRE(nodes.size() == 3);
}

TEST_CASE("Persistent-undo settings default and round-trip", "[PersistentUndo]") {
    const PersistentUndoSettingsGuard guard;

    REQUIRE(PersistentUndoEnabled());
    REQUIRE(PersistentUndoMaxSizeMb() == 16);

    SetPersistentUndoEnabled(false);
    REQUIRE_FALSE(PersistentUndoEnabled());

    SetPersistentUndoMaxSizeMb(0); // clamped to 1, TabWidth::SetTabWidth's own convention
    REQUIRE(PersistentUndoMaxSizeMb() == 1);
}
