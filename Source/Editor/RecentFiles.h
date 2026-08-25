//
// Cross-session "recently opened files" list (Emacs recentf equivalent) --
// editor-ergonomics follow-up. Global and project-agnostic, same design call
// Session.h's save-place already made: a file opened outside any project is
// exactly as much "recently opened" as one inside a project, and a single
// shared list is what lets find-recent-file jump between projects, which is
// the case this is most useful for.
//
// RecentFilesStore is the pure, unit-testable core (JSON round-trip, LRU
// cap, path-key normalization -- deliberately duplicating FilePlaceStore::
// NormalizePathKey rather than depending on Session.h for it, that file's
// own precedent for why); the process-wide accessors below wrap one
// mutex-guarded static instance, mirroring Session.h's exact pattern.
//
// Unlike FilePlace, there is no per-file payload beyond "was opened, when"
// -- this tracks visits, not content.
//

#ifndef NED_EDITOR_RECENTFILES_H
#define NED_EDITOR_RECENTFILES_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor {

class RecentFilesStore {
  public:
    // Comfortably above any realistic "recently opened" working set --
    // FilePlaceStore::kMaxEntries' own reasoning.
    static constexpr std::size_t kMaxEntries = 200;

    // Records path (normalized) as just-opened, stamping lastUsed (nowSeconds
    // is injectable for tests only; the default is the real clock). Moves an
    // already-present entry to the front rather than duplicating it. Evicts
    // the least-recently-used entry past kMaxEntries.
    void Record(const std::filesystem::path& path, std::optional<std::int64_t> nowSeconds = std::nullopt);

    // Most-recent-first path strings (normalized keys, as stored).
    [[nodiscard]] std::vector<std::string> Paths() const;

    // Missing file loads as an empty store; a malformed/unparseable one is
    // discarded the same way -- this is convenience state, not user content,
    // losing it must never block startup. SaveToFile writes via a sibling
    // .ned-tmp + rename, mirroring FilePlaceStore::SaveToFile, creating
    // parent directories first; throws std::runtime_error on I/O failure.
    void LoadFromFile(const std::filesystem::path& path);
    void SaveToFile(const std::filesystem::path& path) const;

    [[nodiscard]] std::string ToJson() const;
    static RecentFilesStore   FromJson(std::string_view json); // malformed -> empty store
    [[nodiscard]] std::size_t Count() const;
    [[nodiscard]] bool        Dirty() const;
    void                      ClearDirty();

    // Same normalization FilePlaceStore::NormalizePathKey performs,
    // duplicated rather than shared -- see that method's own doc comment.
    [[nodiscard]] static std::string NormalizePathKey(const std::filesystem::path& path);

  private:
    struct Entry {
        std::int64_t lastUsed = 0; // Unix seconds
    };

    void EvictPastCap();

    std::map<std::string, Entry> entries_; // keyed by normalized path
    bool                         dirty_ = false;
};

// -- Process-wide store + toggle (mutex-guarded static state) -----------------

// Configured from Janet via ned/set-recentf-enabled; default on. Off
// disables both restore *and* recording, SavePlaceEnabled's own shape.
void               SetRecentFilesEnabled(bool enabled);
[[nodiscard]] bool RecentFilesEnabled();

// $XDG_STATE_HOME/ned/recent-files.json (falling back to
// ~/.local/state/ned/recent-files.json). A pure path calculation like
// FilePlacesPath; throws std::runtime_error if neither XDG_STATE_HOME nor
// HOME is set.
[[nodiscard]] std::filesystem::path RecentFilesPath();

// Loads/saves the process-wide store at RecentFilesPath(). Both swallow
// every failure -- LoadRecentFiles runs once during startup and
// SaveRecentFiles runs unattended on the auto-save timer, FilePlaceStore's
// own reasoning. SaveRecentFiles skips the write outright when nothing
// Dirty() unless force is set.
void LoadRecentFiles();
void SaveRecentFiles(bool force = false);

// Records buffer's path as just-opened/just-switched-to. A no-op when
// disabled, when buffer has no path, or while it's still an IsLoading()
// async placeholder -- RecordFilePlace's own guard conditions.
void RecordRecentFile(const text::Buffer& buffer);

// Most-recent-first path strings for the find-recent-file picker (nullopt
// -- rather, empty -- when disabled). Absolute/normalized paths, not
// project-relative -- unlike project-find-file's candidates, these can span
// multiple projects.
[[nodiscard]] std::vector<std::string> RecentFilePaths();

// Tests only: the process-wide store back to empty + enabled.
void ResetRecentFilesForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_RECENTFILES_H
