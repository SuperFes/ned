#include "TransientSession.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <string>
#include <string_view>

#include "Backup.h"
#include "PersistentUndo.h"
#include "RecentFiles.h"
#include "Session.h"

namespace ned::editor {

namespace {

    std::mutex& TransientMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& TransientStorage() {
        static bool enabled = false;
        return enabled;
    }

    [[nodiscard]] bool HasAffixes(std::string_view name, std::string_view prefix, std::string_view suffix) {
        return name.size() > prefix.size() + suffix.size() && name.starts_with(prefix) && name.ends_with(suffix);
    }

    // The infix between prefix and suffix, for the two entries that care what
    // it looks like.
    [[nodiscard]] std::string_view Infix(std::string_view name, std::string_view prefix, std::string_view suffix) {
        return name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    }

} // namespace

bool IsVcsEditorFile(const std::filesystem::path& path) {
    const std::string name = path.filename().string();

    // git: fixed, compiled into the binary -- no configuration changes them,
    // and commit.template/-t only changes where the *initial content* comes
    // from, never the path handed over.
    static constexpr std::array<std::string_view, 7> kExactNames{
        "COMMIT_EDITMSG",
        "MERGE_MSG",
        "TAG_EDITMSG",
        "SQUASH_MSG",
        "NOTES_EDITMSG",
        "git-rebase-todo",
        "addp-hunk-edit.diff",
    };
    if (std::ranges::find(kExactNames, name) != kExactNames.end()) {
        return true;
    }

    // svn: svn-commit.tmp, then svn-commit.2.tmp, svn-commit.3.tmp ... for
    // subsequent attempts in the same working copy.
    if (name == "svn-commit.tmp" || (HasAffixes(name, "svn-commit.", ".tmp") &&
                                     std::ranges::all_of(Infix(name, "svn-commit.", ".tmp"),
                                                         [](unsigned char c) { return std::isdigit(c) != 0; }))) {
        return true;
    }

    // hg: hg-editor-<random>.commit.hg.txt today; older versions dropped the
    // .commit.hg part, so the prefix plus a .txt suffix is what both share.
    if (HasAffixes(name, "hg-editor-", ".txt")) {
        return true;
    }

    // jj: editor-<random>.jjdescription. The suffix alone is distinctive --
    // the prefix is not, and jj is free to change it.
    if (name.size() > std::string_view(".jjdescription").size() && name.ends_with(".jjdescription")) {
        return true;
    }

    // fossil: ci-comment-<uppercase hex>.txt. Matched against a hex infix
    // rather than a bare glob, so a real ci-comment-template.txt in a
    // repository's CI configuration is never claimed -- see the header.
    if (HasAffixes(name, "ci-comment-", ".txt")) {
        const std::string_view infix = Infix(name, "ci-comment-", ".txt");
        if (std::ranges::all_of(infix, [](unsigned char c) { return std::isxdigit(c) != 0; })) {
            return true;
        }
    }

    return false;
}

void SetTransientMode(bool enabled) {
    const std::lock_guard<std::mutex> lock(TransientMutex());
    TransientStorage() = enabled;
}

bool TransientMode() {
    const std::lock_guard<std::mutex> lock(TransientMutex());
    return TransientStorage();
}

void ApplyTransientMode() {
    if (!TransientMode()) {
        return;
    }
    SetSavePlaceEnabled(false);
    SetRecentFilesEnabled(false);
    SetPersistentUndoEnabled(false);
    SetFileAutoSaveEnabled(false);   // the periodic crash-recovery snapshot
    SetBackupVersionsEnabled(false); // the pre-save version copy
}

void ResetTransientModeForTesting() {
    SetTransientMode(false);
}

} // namespace ned::editor
