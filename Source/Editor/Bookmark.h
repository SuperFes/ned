//
// Named, persistent point locations (Emacs bookmark.el equivalent) --
// editor-ergonomics follow-up. Global and project-agnostic, Session.h's own
// design call: a bookmark is most useful for jumping *between* projects
// (a project file and its docs/config elsewhere), so one shared list beats
// per-project namespacing.
//
// Unlike RegisterTable (Register.h -- single-character key, in-memory only,
// cleared on process exit), a bookmark has a user-chosen name and survives
// restarts, persisted to $XDG_STATE_HOME/ned/bookmarks.json the same
// JSON-file-via-mutex-guarded-static-store shape Session.h/RecentFiles.h
// both use. Stores (line, column), never a byte offset -- FilePlace's own
// reasoning: a file edited outside ned between runs makes a byte offset
// silently wrong, while a line/column clamps sanely via
// Buffer::ByteOffsetForLineAndColumn.
//
// BookmarkStore is the pure, unit-testable core; the process-wide
// accessors below wrap one mutex-guarded static instance. Opening the
// target file and actually moving point is UI-layer work (BufferList/
// ActiveBuffer access) left to BufferView, the same division Session.h
// draws between RestoreFilePlace (editor-layer, given an already-open
// Buffer&) and find-file's own file-opening (UI-layer).
//

#ifndef NED_EDITOR_BOOKMARK_H
#define NED_EDITOR_BOOKMARK_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor {

struct Bookmark {
    std::string name;
    std::string path;       // normalized key, RecentFilesStore::NormalizePathKey's own convention
    std::size_t line   = 0; // 0-based
    std::size_t column = 0; // 0-based visual column (tab-aware)

    bool operator==(const Bookmark&) const = default;
};

class BookmarkStore {
  public:
    // Sets (overwriting any existing bookmark of the same name).
    void Set(Bookmark mark);

    // True if a bookmark named name existed and was removed.
    bool Delete(const std::string& name);

    [[nodiscard]] std::optional<Bookmark> Find(const std::string& name) const;

    // Sorted alphabetically -- a picker's candidate list, not an MRU order
    // (real Emacs' own bookmark-jump lists this way too; there's no
    // "recently jumped to" concept for bookmarks the way RecentFiles has).
    [[nodiscard]] std::vector<std::string> Names() const;

    // Missing file loads as an empty store; a malformed/unparseable one is
    // discarded the same way. SaveToFile writes via a sibling .ned-tmp +
    // rename, mirroring FilePlaceStore::SaveToFile.
    void LoadFromFile(const std::filesystem::path& path);
    void SaveToFile(const std::filesystem::path& path) const;

    [[nodiscard]] std::string ToJson() const;
    static BookmarkStore      FromJson(std::string_view json); // malformed -> empty store
    [[nodiscard]] std::size_t Count() const;
    [[nodiscard]] bool        Dirty() const;
    void                      ClearDirty();

  private:
    std::map<std::string, Bookmark> entries_; // keyed by name
    bool                            dirty_ = false;
};

// -- Process-wide store (mutex-guarded static state) -------------------------

// $XDG_STATE_HOME/ned/bookmarks.json (falling back to
// ~/.local/state/ned/bookmarks.json). Throws std::runtime_error if neither
// XDG_STATE_HOME nor HOME is set, FilePlacesPath/RecentFilesPath's own
// contract.
[[nodiscard]] std::filesystem::path BookmarksPath();

// Loads/saves the process-wide store at BookmarksPath(). Both swallow every
// failure, RecentFiles.h's own reasoning. SaveBookmarks skips the write
// outright when nothing Dirty() unless force is set.
void LoadBookmarks();
void SaveBookmarks(bool force = false);

// Records a bookmark named name at buffer's current point (computed via
// tabWidth, RecordFilePlace's own math). A no-op if buffer has no path.
// Marks the store dirty unconditionally -- unlike RecordFilePlace, this is
// always a deliberate, infrequent user action (bookmark-set), never a
// per-keystroke/per-tick call, so there's no "skip an unchanged bump" case
// worth optimizing for.
void RecordBookmark(const std::string& name, const text::Buffer& buffer, std::size_t tabWidth);

bool                                   DeleteBookmark(const std::string& name);
[[nodiscard]] std::optional<Bookmark>  FindBookmark(const std::string& name);
[[nodiscard]] std::vector<std::string> BookmarkNames();

// Tests only: the process-wide store back to empty.
void ResetBookmarksForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_BOOKMARK_H
