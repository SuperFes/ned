#include "Session.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

namespace ned::editor {

namespace {

    using Json = nlohmann::json;

    std::int64_t NowSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    std::mutex& SessionMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& SavePlaceStorage() {
        static bool enabled = true;
        return enabled;
    }

    FilePlaceStore& StoreStorage() {
        static FilePlaceStore store;
        return store;
    }

} // namespace

std::string FilePlaceStore::NormalizePathKey(const std::filesystem::path& path) {
    std::error_code             ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonical.string();
    }
    return std::filesystem::absolute(path).string();
}

std::optional<FilePlace> FilePlaceStore::Lookup(const std::filesystem::path& path) const {
    const auto it = entries_.find(NormalizePathKey(path));
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second.place;
}

void FilePlaceStore::Record(const std::filesystem::path& path, FilePlace place,
                            std::optional<std::int64_t> nowSeconds) {
    const std::int64_t now    = nowSeconds.value_or(NowSeconds());
    const auto [it, inserted] = entries_.try_emplace(NormalizePathKey(path));
    Entry& entry              = it->second;

    if (!place.topLine) {
        place.topLine = entry.place.topLine; // merge -- see FilePlace::topLine's doc comment
    }
    if (inserted || entry.place != place) {
        entry.place = place;
        dirty_      = true;
    }
    entry.lastUsed = now;

    EvictPastCap();
}

void FilePlaceStore::Touch(const std::filesystem::path& path, std::optional<std::int64_t> nowSeconds) {
    const auto it = entries_.find(NormalizePathKey(path));
    if (it != entries_.end()) {
        it->second.lastUsed = nowSeconds.value_or(NowSeconds());
    }
}

void FilePlaceStore::EvictPastCap() {
    while (entries_.size() > kMaxEntries) {
        auto oldest = entries_.begin();
        for (auto it = std::next(entries_.begin()); it != entries_.end(); ++it) {
            if (it->second.lastUsed < oldest->second.lastUsed) {
                oldest = it;
            }
        }
        entries_.erase(oldest);
        dirty_ = true;
    }
}

void FilePlaceStore::LoadFromFile(const std::filesystem::path& path) {
    entries_.clear();
    dirty_ = false;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return; // missing/unreadable -> empty store, by contract
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    *this = FromJson(content);
}

void FilePlaceStore::SaveToFile(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());

    const std::filesystem::path temporary = path.string() + ".ned-tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot write file places to \"" + temporary.string() + "\"");
        }
        file << ToJson();
        if (!file.flush()) {
            throw std::runtime_error("ned: failed writing file places to \"" + temporary.string() + "\"");
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string FilePlaceStore::ToJson() const {
    Json places = Json::array();
    for (const auto& [key, entry] : entries_) {
        Json place = {
            {"path", key},
            {"line", entry.place.line},
            {"column", entry.place.column},
            {"lastUsed", entry.lastUsed},
        };
        if (entry.place.topLine) {
            place["topLine"] = *entry.place.topLine;
        }
        places.push_back(std::move(place));
    }
    return Json{{"version", 1}, {"places", std::move(places)}}.dump(2);
}

FilePlaceStore FilePlaceStore::FromJson(std::string_view json) {
    FilePlaceStore store;
    try {
        const Json parsed = Json::parse(json);
        for (const Json& place : parsed.value("places", Json::array())) {
            if (!place.is_object() || !place.contains("path") || !place["path"].is_string()) {
                continue; // one malformed entry shouldn't discard the rest
            }
            Entry entry;
            entry.place.line   = place.value("line", std::size_t{0});
            entry.place.column = place.value("column", std::size_t{0});
            entry.lastUsed     = place.value("lastUsed", std::int64_t{0});
            if (place.contains("topLine") && place["topLine"].is_number_unsigned()) {
                entry.place.topLine = place["topLine"].get<std::size_t>();
            }
            // Stored keys were normalized at Record time; inserted verbatim
            // rather than re-normalized, so loading never pays a filesystem
            // call per entry.
            store.entries_.emplace(place["path"].get<std::string>(), std::move(entry));
        }
        store.EvictPastCap();
        store.dirty_ = false; // freshly loaded content is by definition in sync
    }
    catch (const std::exception&) {
        return FilePlaceStore{}; // malformed -> empty, by contract
    }
    return store;
}

std::size_t FilePlaceStore::Count() const {
    return entries_.size();
}

bool FilePlaceStore::Dirty() const {
    return dirty_;
}

void FilePlaceStore::ClearDirty() {
    dirty_ = false;
}

// -- Process-wide store + toggle ----------------------------------------------

void SetSavePlaceEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SessionMutex());
    SavePlaceStorage() = enabled;
}

bool SavePlaceEnabled() {
    const std::lock_guard<std::mutex> lock(SessionMutex());
    return SavePlaceStorage();
}

std::filesystem::path FilePlacesPath() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "file-places.json";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "file-places.json";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

void LoadFilePlaces() {
    try {
        const std::filesystem::path       path = FilePlacesPath();
        const std::lock_guard<std::mutex> lock(SessionMutex());
        StoreStorage().LoadFromFile(path);
    }
    catch (const std::exception&) {
        // Swallowed -- see this function's own doc comment.
    }
}

void SaveFilePlaces(bool force) {
    try {
        const std::filesystem::path       path = FilePlacesPath();
        const std::lock_guard<std::mutex> lock(SessionMutex());
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

void RestoreFilePlace(text::Buffer& buffer, std::size_t tabWidth) {
    if (!buffer.Path() || buffer.IsLoading()) {
        return;
    }

    std::optional<FilePlace> place;
    {
        const std::lock_guard<std::mutex> lock(SessionMutex());
        if (!SavePlaceStorage()) {
            return;
        }
        place = StoreStorage().Lookup(*buffer.Path());
        if (place) {
            StoreStorage().Touch(*buffer.Path());
        }
    }
    if (!place) {
        return;
    }

    buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(place->line, place->column, tabWidth));
}

std::optional<FilePlace> StoredFilePlaceFor(const text::Buffer& buffer) {
    if (!buffer.Path()) {
        return std::nullopt;
    }

    const std::lock_guard<std::mutex> lock(SessionMutex());
    if (!SavePlaceStorage()) {
        return std::nullopt;
    }
    return StoreStorage().Lookup(*buffer.Path());
}

void RecordFilePlace(const text::Buffer& buffer, std::optional<std::size_t> topLine, std::size_t tabWidth) {
    if (!buffer.Path() || buffer.IsLoading()) {
        return;
    }

    const std::size_t point     = buffer.Point();
    const std::size_t line      = buffer.Content().ByteOffsetToLine(point);
    const std::size_t lineStart = buffer.Content().LineToByteOffset(line);
    const std::size_t column    = buffer.VisualColumnForByteOffset(lineStart, point, tabWidth);

    const std::lock_guard<std::mutex> lock(SessionMutex());
    if (!SavePlaceStorage()) {
        return;
    }
    StoreStorage().Record(*buffer.Path(), FilePlace{.line = line, .column = column, .topLine = topLine});
}

void ResetFilePlacesForTesting() {
    const std::lock_guard<std::mutex> lock(SessionMutex());
    StoreStorage()     = FilePlaceStore{};
    SavePlaceStorage() = true;
}

} // namespace ned::editor
