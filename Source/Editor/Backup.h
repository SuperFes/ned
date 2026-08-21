//
// File preservation (backup-and-recovery follow-up): Emacs-grade backups and
// crash-recovery auto-saves, but stored in one ned-managed XDG state
// directory rather than as sibling files littering the edited file's own
// directory (the explicit user complaint about Emacs' scheme this replaces).
// Two tracks, matching Emacs' proven model:
//
//   - Backup versions: on each save over an *existing* file, the file's
//     prior on-disk content is preserved as a timestamped version (history).
//     Hooked from Commands.cpp's save paths, never from Buffer::Save itself
//     (Text/ stays policy-free, and scratch auto-save calls Buffer::Save
//     directly -- a Buffer-level hook would version scratches every tick).
//   - Auto-saves: one rotating `autosave` snapshot per modified,
//     path-associated, non-scratch file buffer, refreshed on the existing
//     5-second timer tick (WindowManager::StartAutoSaveTimer) and deleted by
//     a real save (crash recovery).
//
// Layout: BackupsDirectory()/<fnv1a64-of-normalized-path>/ per file, holding
// a `path` sidecar (the hash is one-way, so the original path is recorded
// where cleanup and future tooling can read it back), `v-<UTC
// timestamp>-<seq>.bak` versions, and the `autosave` snapshot. Retention is
// ned's own job (no daemon): PruneBackups runs at startup and, rate-limited,
// on the same timer tick.
//

#ifndef NED_EDITOR_BACKUP_H
#define NED_EDITOR_BACKUP_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Text/BufferList.h"

namespace ned::editor {

// $XDG_STATE_HOME/ned/backups, falling back to $HOME/.local/state/ned/backups
// if XDG_STATE_HOME is unset or empty. Throws std::runtime_error if neither
// is usable. Backups are disposable editor state, not user content, so this
// mirrors Session.h's FilePlacesPath resolution ($XDG_STATE_HOME), not
// ScratchPad.h's ($XDG_DATA_HOME). A pure path calculation -- does not create
// the directory; the write sites below do.
[[nodiscard]] std::filesystem::path BackupsDirectory();

// BackupsDirectory() / Fnv1a64Hex(FilePlaceStore::NormalizePathKey(file)) --
// the exact per-key filename recipe ProjectSessionPath already uses, so two
// symlinked opens of the same file share one history. Pure calculation.
[[nodiscard]] std::filesystem::path BackupDirectoryForFile(const std::filesystem::path& file);

// One recoverable snapshot of a file, as listed by ListBackupVersions.
struct BackupVersion {
    std::filesystem::path path;                 // absolute path of the version file itself
    std::string           label;                // "autosave (crash recovery)", or a local-time stamp
    std::int64_t          timestampSeconds = 0; // Unix seconds: parsed from the filename (versions) or mtime (autosave)
    bool                  isAutoSave       = false;
};

// Every recoverable snapshot for file: the autosave first if one exists, then
// backup versions newest-first. Returns an empty list if the file has no
// backup directory yet or it can't be listed (nothing to recover, not an
// error -- ListScratchNames' convention).
[[nodiscard]] std::vector<BackupVersion> ListBackupVersions(const std::filesystem::path& file);

// The version file's full content. Throws std::runtime_error on any read
// failure -- unlike the write side, a failed *recovery* has a user waiting on
// it who needs to hear why.
[[nodiscard]] std::string ReadBackupVersion(const std::filesystem::path& versionPath);

// Preserves file's current on-disk content as a new timestamped version --
// called immediately before a save's rename clobbers it, which also captures
// any content written externally since the buffer loaded. A silent no-op when
// there's nothing to preserve or preserving is inappropriate: the file
// doesn't exist yet (first save), it lives directly inside ScratchDirectory()
// (scratches are already auto-saved originals, not copies), or it exceeds the
// size cutoff (see Backup.cpp's kMaxBackupBytes). Swallows every filesystem
// error internally -- a failed backup must never block the save it precedes.
// nowSeconds is injectable for tests only (FilePlaceStore's convention) --
// it's what makes same-second sequence numbering deterministic to assert on.
void BackupFileBeforeSave(const std::filesystem::path& file, std::optional<std::int64_t> nowSeconds = std::nullopt);

// Writes content as file's `autosave` snapshot (creating the backup directory
// and `path` sidecar as needed), via the usual sibling-.ned-tmp-then-rename
// atomic write. Throws on failure; AutoSaveFileBuffers is the caller that
// swallows, per-buffer.
void WriteAutoSave(const std::filesystem::path& file, std::string_view content);

// Deletes file's `autosave` snapshot (the buffer was really saved, so the
// crash snapshot is obsolete) and forgets its generation memo entry so a
// later re-modification writes a fresh one immediately. Swallows errors --
// called on the save path, where the save itself already succeeded.
void RemoveAutoSave(const std::filesystem::path& file);

// The timer-tick rider (AutoSaveScratchBuffers' analogue for regular files):
// writes an autosave for every buffer that is path-associated, done loading,
// not a scratch, Modified() (which excludes the transient PreviewBuffer()
// for free -- it self-promotes to a real buffer the moment it's modified,
// so an unmodified preview is already skipped as unmodified), and whose
// ContentGeneration() actually changed since this process last wrote its
// autosave (a mutex-guarded memo -- Paint-adjacent ticks vastly outnumber
// edits, and an unchanged snapshot needn't be rewritten every 5 seconds). A
// no-op when FileAutoSaveEnabled() is off. Per-buffer failures are swallowed,
// same reasoning as AutoSaveScratchBuffers: unattended timer, next tick
// retries.
void AutoSaveFileBuffers(text::BufferList& bufferList);

// Retention: per backup directory, deletes versions older than
// BackupMaxAgeDays(), then keeps only the BackupMaxVersions() newest; deletes
// an `autosave` whose mtime is past the age limit (an orphan -- its editor
// never came back for it); removes a directory left holding only its `path`
// sidecar. nowSeconds is injectable for tests only (FilePlaceStore's
// convention), defaulting to the current time. Swallows all errors.
void PruneBackups(std::optional<std::int64_t> nowSeconds = std::nullopt);

// PruneBackups, rate-limited to at most once per hour of process lifetime --
// the form the timer tick calls, so cleanup stays opportunistic without
// re-walking the whole backup tree every 5 seconds.
void MaybePruneBackups(std::optional<std::int64_t> nowSeconds = std::nullopt);

// Process-wide settings (mutex-guarded static state, mirroring
// ScratchPad.h/TabWidth.h's exact pattern), each configured from Janet:
// ned/set-file-auto-save, ned/set-backup-max-age-days,
// ned/set-backup-max-versions. Age/version limits <= 0 disable that pruning
// dimension entirely.
void               SetFileAutoSaveEnabled(bool enabled);
[[nodiscard]] bool FileAutoSaveEnabled(); // default true
void               SetBackupMaxAgeDays(int days);
[[nodiscard]] int  BackupMaxAgeDays(); // default 14
void               SetBackupMaxVersions(int versions);
[[nodiscard]] int  BackupMaxVersions(); // default 20
// Resets every setting above to its default and clears the auto-save
// generation memo and MaybePruneBackups' last-run stamp -- process-wide
// statics leak between Catch2 cases otherwise (ResetFilePlacesForTesting's
// convention).
void ResetBackupsForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_BACKUP_H
