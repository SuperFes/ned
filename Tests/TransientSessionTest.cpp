#include <catch2/catch_test_macros.hpp>

#include "Editor/Backup.h"
#include "Editor/PersistentUndo.h"
#include "Editor/RecentFiles.h"
#include "Editor/Session.h"
#include "Editor/TransientSession.h"

using ned::editor::ApplyTransientMode;
using ned::editor::IsVcsEditorFile;
using ned::editor::SetTransientMode;
using ned::editor::TransientMode;

namespace {

// Process-wide statics leak between Catch2 cases otherwise
// (ResetBackupsForTesting's own convention).
struct TransientGuard {
    ~TransientGuard() {
        ned::editor::ResetTransientModeForTesting();
        ned::editor::SetSavePlaceEnabled(true);
        ned::editor::SetRecentFilesEnabled(true);
        ned::editor::SetPersistentUndoEnabled(true);
        ned::editor::ResetBackupsForTesting();
    }
};

} // namespace

// Every name below came from running the tool against a probe editor, not
// from reading its source -- see TransientSession.h.

TEST_CASE("IsVcsEditorFile claims every file git hands its editor", "[TransientSession]") {
    CHECK(IsVcsEditorFile("/repo/.git/COMMIT_EDITMSG"));
    CHECK(IsVcsEditorFile("/repo/.git/MERGE_MSG"));
    CHECK(IsVcsEditorFile("/repo/.git/TAG_EDITMSG"));
    CHECK(IsVcsEditorFile("/repo/.git/SQUASH_MSG"));
    CHECK(IsVcsEditorFile("/repo/.git/NOTES_EDITMSG"));
    CHECK(IsVcsEditorFile("/repo/.git/rebase-merge/git-rebase-todo"));
    CHECK(IsVcsEditorFile("/repo/.git/addp-hunk-edit.diff"));
}

TEST_CASE("IsVcsEditorFile keys on the basename, never the directory", "[TransientSession]") {
    // git's directory varies far more than its filenames do: a worktree, a
    // submodule and a relocated GIT_DIR all put the same name somewhere
    // else, so a rule about path shape would be the fragile half.
    CHECK(IsVcsEditorFile("/repo/.git/worktrees/feature/COMMIT_EDITMSG"));
    CHECK(IsVcsEditorFile("/repo/.git/modules/sub/COMMIT_EDITMSG"));
    CHECK(IsVcsEditorFile("/var/tmp/somewhere-else/COMMIT_EDITMSG"));
    // svn and fossil hand over a relative path, unlike git and hg.
    CHECK(IsVcsEditorFile("svn-commit.tmp"));
    CHECK(IsVcsEditorFile("./ci-comment-D1E918EEE361.txt"));
}

TEST_CASE("IsVcsEditorFile claims the other four VCS tools' editor files", "[TransientSession]") {
    CHECK(IsVcsEditorFile("svn-commit.tmp"));
    CHECK(IsVcsEditorFile("svn-commit.2.tmp")); // second attempt in the same working copy
    CHECK(IsVcsEditorFile("/tmp/hg-editor-s7m2johp.commit.hg.txt"));
    CHECK(IsVcsEditorFile("/tmp/editor-vdr8cz.jjdescription"));
    CHECK(IsVcsEditorFile("./ci-comment-D1E918EEE361.txt"));
}

TEST_CASE("IsVcsEditorFile leaves ordinary files alone", "[TransientSession]") {
    CHECK_FALSE(IsVcsEditorFile("/repo/Source/main.cpp"));
    CHECK_FALSE(IsVcsEditorFile("/repo/notes.txt"));
    CHECK_FALSE(IsVcsEditorFile("/repo/commit_editmsg")); // git's names are upper-case and exact
    CHECK_FALSE(IsVcsEditorFile("/repo/COMMIT_EDITMSG.bak"));
    CHECK_FALSE(IsVcsEditorFile("/repo/my-COMMIT_EDITMSG"));
    CHECK_FALSE(IsVcsEditorFile("/repo/svn-commit.tmp.orig"));
    CHECK_FALSE(IsVcsEditorFile(""));
}

TEST_CASE("IsVcsEditorFile does not claim a real ci-comment file", "[TransientSession]") {
    // The one entry narrow enough to need a hex infix rather than a glob: a
    // repository with CI configuration plausibly has files like these, and
    // claiming one would silently stop recording a file being worked in.
    CHECK_FALSE(IsVcsEditorFile("/repo/ci-comment-template.txt"));
    CHECK_FALSE(IsVcsEditorFile("/repo/ci-comment-.txt"));
    CHECK_FALSE(IsVcsEditorFile("/repo/ci-comment-notes-v2.txt"));
    CHECK(IsVcsEditorFile("/repo/ci-comment-0123456789ABCDEF.txt"));
}

TEST_CASE("ApplyTransientMode turns off every persistence switch", "[TransientSession]") {
    const TransientGuard guard;

    REQUIRE(ned::editor::SavePlaceEnabled());
    REQUIRE(ned::editor::RecentFilesEnabled());
    REQUIRE(ned::editor::PersistentUndoEnabled());
    REQUIRE(ned::editor::FileAutoSaveEnabled());
    REQUIRE(ned::editor::BackupVersionsEnabled());

    SetTransientMode(true);
    ApplyTransientMode();

    CHECK(TransientMode());
    CHECK_FALSE(ned::editor::SavePlaceEnabled());
    CHECK_FALSE(ned::editor::RecentFilesEnabled());
    CHECK_FALSE(ned::editor::PersistentUndoEnabled());
    CHECK_FALSE(ned::editor::FileAutoSaveEnabled());
    CHECK_FALSE(ned::editor::BackupVersionsEnabled());
}

TEST_CASE("ApplyTransientMode is inert outside transient mode", "[TransientSession]") {
    const TransientGuard guard;

    SetTransientMode(false);
    ApplyTransientMode();

    CHECK(ned::editor::SavePlaceEnabled());
    CHECK(ned::editor::RecentFilesEnabled());
    CHECK(ned::editor::PersistentUndoEnabled());
    CHECK(ned::editor::FileAutoSaveEnabled());
    CHECK(ned::editor::BackupVersionsEnabled());
}
