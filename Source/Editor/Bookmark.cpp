#include "Bookmark.h"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "RecentFiles.h"

namespace ned::editor {

namespace {

    using Json = nlohmann::json;

    std::mutex& BookmarkMutex() {
        static std::mutex mutex;
        return mutex;
    }

    BookmarkStore& StoreStorage() {
        static BookmarkStore store;
        return store;
    }

} // namespace

void BookmarkStore::Set(Bookmark mark) {
    const std::string name = mark.name;
    entries_[name]         = std::move(mark);
    dirty_                 = true;
}

bool BookmarkStore::Delete(const std::string& name) {
    const bool erased = entries_.erase(name) > 0;
    if (erased) {
        dirty_ = true;
    }
    return erased;
}

std::optional<Bookmark> BookmarkStore::Find(const std::string& name) const {
    const auto it = entries_.find(name);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> BookmarkStore::Names() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto& [name, mark] : entries_) {
        names.push_back(name);
    }
    return names; // std::map already iterates in sorted key order
}

void BookmarkStore::LoadFromFile(const std::filesystem::path& path) {
    entries_.clear();
    dirty_ = false;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return; // missing/unreadable -> empty store, by contract
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    *this = FromJson(content);
}

void BookmarkStore::SaveToFile(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());

    const std::filesystem::path temporary = path.string() + ".ned-tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot write bookmarks to \"" + temporary.string() + "\"");
        }
        file << ToJson();
        if (!file.flush()) {
            throw std::runtime_error("ned: failed writing bookmarks to \"" + temporary.string() + "\"");
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string BookmarkStore::ToJson() const {
    Json marks = Json::array();
    for (const auto& [name, mark] : entries_) {
        marks.push_back(Json{
            {"name", mark.name},
            {"path", mark.path},
            {"line", mark.line},
            {"column", mark.column},
        });
    }
    return Json{{"version", 1}, {"bookmarks", std::move(marks)}}.dump(2);
}

BookmarkStore BookmarkStore::FromJson(std::string_view json) {
    BookmarkStore store;
    try {
        const Json parsed = Json::parse(json);
        for (const Json& entry : parsed.value("bookmarks", Json::array())) {
            if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() ||
                !entry.contains("path") || !entry["path"].is_string()) {
                continue; // one malformed entry shouldn't discard the rest
            }
            Bookmark mark;
            mark.name   = entry["name"].get<std::string>();
            mark.path   = entry["path"].get<std::string>();
            mark.line   = entry.value("line", std::size_t{0});
            mark.column = entry.value("column", std::size_t{0});
            store.entries_.emplace(mark.name, std::move(mark));
        }
        store.dirty_ = false; // freshly loaded content is by definition in sync
    }
    catch (const std::exception&) {
        return BookmarkStore{}; // malformed -> empty, by contract
    }
    return store;
}

std::size_t BookmarkStore::Count() const {
    return entries_.size();
}

bool BookmarkStore::Dirty() const {
    return dirty_;
}

void BookmarkStore::ClearDirty() {
    dirty_ = false;
}

// -- Process-wide store --------------------------------------------------

std::filesystem::path BookmarksPath() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "bookmarks.json";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "bookmarks.json";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

void LoadBookmarks() {
    try {
        const std::filesystem::path       path = BookmarksPath();
        const std::lock_guard<std::mutex> lock(BookmarkMutex());
        StoreStorage().LoadFromFile(path);
    }
    catch (const std::exception&) {
        // Swallowed -- see this function's own doc comment.
    }
}

void SaveBookmarks(bool force) {
    try {
        const std::filesystem::path       path = BookmarksPath();
        const std::lock_guard<std::mutex> lock(BookmarkMutex());
        if (!force && !StoreStorage().Dirty()) {
            return;
        }
        StoreStorage().SaveToFile(path);
        StoreStorage().ClearDirty();
    }
    catch (const std::exception&) {
        // Swallowed -- see this function's own doc comment.
    }
}

void RecordBookmark(const std::string& name, const text::Buffer& buffer, std::size_t tabWidth) {
    if (!buffer.Path()) {
        return;
    }

    const std::size_t point     = buffer.Point();
    const std::size_t line      = buffer.Content().ByteOffsetToLine(point);
    const std::size_t lineStart = buffer.Content().LineToByteOffset(line);
    const std::size_t column    = buffer.VisualColumnForByteOffset(lineStart, point, tabWidth);

    const std::lock_guard<std::mutex> lock(BookmarkMutex());
    // Reuses RecentFilesStore's normalization rather than a third copy of
    // it (Session.h's own duplication precedent doesn't apply between two
    // siblings introduced together) -- both stores need paths to compare
    // equal regardless of relative/symlinked spelling.
    StoreStorage().Set(Bookmark{
        .name   = name,
        .path   = RecentFilesStore::NormalizePathKey(*buffer.Path()),
        .line   = line,
        .column = column,
    });
}

bool DeleteBookmark(const std::string& name) {
    const std::lock_guard<std::mutex> lock(BookmarkMutex());
    return StoreStorage().Delete(name);
}

std::optional<Bookmark> FindBookmark(const std::string& name) {
    const std::lock_guard<std::mutex> lock(BookmarkMutex());
    return StoreStorage().Find(name);
}

std::vector<std::string> BookmarkNames() {
    const std::lock_guard<std::mutex> lock(BookmarkMutex());
    return StoreStorage().Names();
}

void ResetBookmarksForTesting() {
    const std::lock_guard<std::mutex> lock(BookmarkMutex());
    StoreStorage() = BookmarkStore{};
}

} // namespace ned::editor
