//
// Per-file place persistence ("save-place") -- session-persistence slice 1.
//
// Remembers, per file, where point (and the viewport's top line, when a
// pane was actually showing the file) last was, across editor runs, in
// $XDG_STATE_HOME/ned/file-places.json -- state, not config or data, hence
// the one XDG base directory this codebase didn't use yet. Global and
// project-agnostic on purpose: it applies to `ned ~/notes.txt` from a
// non-project directory exactly as much as to a project file. Per-project
// session restore (open-buffer sets, breakpoints, .ned/) is slice 2 and
// builds beside this, not on top of it.
//
// FilePlaceStore is the pure, unit-testable core (JSON round-trip, LRU
// cap, path-key normalization); the process-wide accessors below wrap one
// mutex-guarded static instance, mirroring TabWidth.h/ProjectRoot.h's
// exact pattern -- static because the restore hook (BufferList's
// on-file-opened callback) and BufferView's top-line restore both need it
// without threading a new reference through every constructor in between.
//
// Places are stored as (line, visual column), never a byte offset -- a
// file edited outside ned between runs makes a byte offset silently wrong,
// while a line/column clamps sanely via Buffer::ByteOffsetForLineAndColumn.
//

#ifndef NED_EDITOR_SESSION_H
#define NED_EDITOR_SESSION_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "Text/Buffer.h"

namespace ned::editor {

struct FilePlace {
    std::size_t line   = 0; // 0-based, matching Rope::ByteOffsetToLine
    std::size_t column = 0; // 0-based visual column (tab-aware), matching ByteOffsetForLineAndColumn
    // First visible viewport line, recorded only for buffers a pane was
    // actually showing at record time -- a buffer open in the background
    // has no viewport, and overwriting a previously stored topLine with
    // "unknown" would discard real information (Record merges instead).
    std::optional<std::size_t> topLine;

    bool operator==(const FilePlace&) const = default;
};

class FilePlaceStore {
  public:
    // Comfortably above any realistic working set, comfortably below a
    // file-places.json worth worrying about -- same "hardcoded C++ for
    // now" scope call kAsyncLoadThreshold made.
    static constexpr std::size_t kMaxEntries = 1000;

    [[nodiscard]] std::optional<FilePlace> Lookup(const std::filesystem::path& path) const;

    // Stores place under path's normalized key, stamping lastUsed (nowSeconds
    // is injectable for tests only; the default is the real clock). A
    // place.topLine of nullopt preserves any previously stored topLine
    // rather than clearing it -- see FilePlace::topLine above. Evicts the
    // least-recently-used entry past kMaxEntries. Only a genuinely changed
    // place (or a new entry) marks the store Dirty() -- a lastUsed bump
    // alone doesn't, so the periodic save can skip rewriting an unchanged
    // file every tick.
    void Record(const std::filesystem::path& path, FilePlace place,
                std::optional<std::int64_t> nowSeconds = std::nullopt);

    // Refreshes path's lastUsed stamp if present (a restore counts as use,
    // keeping actively revisited files from aging out of the LRU cap).
    // Doesn't mark the store Dirty() -- same reasoning as Record's own
    // lastUsed-only case.
    void Touch(const std::filesystem::path& path, std::optional<std::int64_t> nowSeconds = std::nullopt);

    // Missing file loads as an empty store; a malformed/unparseable one is
    // discarded the same way (this is convenience state, not user content
    // -- losing it must never block startup). SaveToFile writes via a
    // sibling .ned-tmp + rename, mirroring Buffer::SaveToFile, creating
    // parent directories first; throws std::runtime_error on I/O failure.
    void LoadFromFile(const std::filesystem::path& path);
    void SaveToFile(const std::filesystem::path& path) const;

    [[nodiscard]] std::string ToJson() const;
    static FilePlaceStore     FromJson(std::string_view json); // malformed -> empty store
    [[nodiscard]] std::size_t Count() const;
    [[nodiscard]] bool        Dirty() const;
    void                      ClearDirty();

    // Public for the same "callers compare keys, never re-derive paths"
    // reason DapManager::NormalizePathKey is (weakly_canonical, falling
    // back to absolute() -- deliberately the same normalization, duplicated
    // rather than depended on: Session has no business pulling in Dap).
    [[nodiscard]] static std::string NormalizePathKey(const std::filesystem::path& path);

  private:
    struct Entry {
        FilePlace    place;
        std::int64_t lastUsed = 0; // Unix seconds
    };

    void EvictPastCap();

    std::map<std::string, Entry> entries_;
    bool                         dirty_ = false;
};

// -- Process-wide store + toggle (mutex-guarded static state) -----------------

// Configured from Janet via ned/set-save-place; default on. Off disables
// both restore *and* recording, Emacs save-place-mode-style.
void               SetSavePlaceEnabled(bool enabled);
[[nodiscard]] bool SavePlaceEnabled();

// $XDG_STATE_HOME/ned/file-places.json (falling back to
// ~/.local/state/ned/file-places.json). A pure path calculation like
// InitFilePath/ScratchDirectory; throws std::runtime_error if neither
// XDG_STATE_HOME nor HOME is set.
[[nodiscard]] std::filesystem::path FilePlacesPath();

// Loads/saves the process-wide store at FilePlacesPath(). Both swallow
// every failure (missing directory, unwritable file, ...) -- LoadFilePlaces
// runs once during startup and SaveFilePlaces runs unattended on the
// auto-save timer, with nothing to report a failure to, the same reasoning
// AutoSaveScratchBuffers documents. SaveFilePlaces skips the write outright
// when nothing Dirty() unless force is set (the quit path forces, so
// lastUsed refreshes still land on disk eventually).
void LoadFilePlaces();
void SaveFilePlaces(bool force = false);

// Applies buffer's stored place, if any: sets point to the stored
// line/column (clamped by ByteOffsetForLineAndColumn) and refreshes the
// entry's lastUsed. A no-op when disabled, when buffer has no path, or
// while it's still an IsLoading() async placeholder (restoring into empty
// placeholder content would clamp to 0 and stick -- a documented slice-1
// gap for >16MiB files, not an oversight). tabWidth is the same explicit
// pass-through Buffer's own tab-aware calls take.
void RestoreFilePlace(text::Buffer& buffer, std::size_t tabWidth);

// buffer's stored place, if any (nullopt when disabled or pathless) --
// BufferView reads topLine out of this at its buffer-switch seam.
[[nodiscard]] std::optional<FilePlace> StoredFilePlaceFor(const text::Buffer& buffer);

// Records buffer's current point (and topLine, when the caller -- a pane
// walk -- knows one) into the process-wide store. Same no-op conditions as
// RestoreFilePlace.
void RecordFilePlace(const text::Buffer& buffer, std::optional<std::size_t> topLine, std::size_t tabWidth);

// Tests only: the process-wide store back to empty + enabled. Static state
// would otherwise leak between test cases.
void ResetFilePlacesForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_SESSION_H
