#include "RecentFiles.h"

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

    std::mutex& RecentFilesMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& RecentFilesEnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    RecentFilesStore& StoreStorage() {
        static RecentFilesStore store;
        return store;
    }

} // namespace

std::string RecentFilesStore::NormalizePathKey(const std::filesystem::path& path) {
    std::error_code             ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonical.string();
    }
    return std::filesystem::absolute(path).string();
}

void RecentFilesStore::Record(const std::filesystem::path& path, std::optional<std::int64_t> nowSeconds) {
    const std::int64_t now                    = nowSeconds.value_or(NowSeconds());
    entries_[NormalizePathKey(path)].lastUsed = now;
    dirty_                                    = true;

    EvictPastCap();
}

std::vector<std::string> RecentFilesStore::Paths() const {
    std::vector<std::pair<std::string, std::int64_t>> ordered;
    ordered.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        ordered.emplace_back(key, entry.lastUsed);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<std::string> paths;
    paths.reserve(ordered.size());
    for (auto& [key, lastUsed] : ordered) {
        paths.push_back(std::move(key));
    }
    return paths;
}

void RecentFilesStore::EvictPastCap() {
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

void RecentFilesStore::LoadFromFile(const std::filesystem::path& path) {
    entries_.clear();
    dirty_ = false;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return; // missing/unreadable -> empty store, by contract
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    *this = FromJson(content);
}

void RecentFilesStore::SaveToFile(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());

    const std::filesystem::path temporary = path.string() + ".ned-tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot write recent files to \"" + temporary.string() + "\"");
        }
        file << ToJson();
        if (!file.flush()) {
            throw std::runtime_error("ned: failed writing recent files to \"" + temporary.string() + "\"");
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string RecentFilesStore::ToJson() const {
    Json files = Json::array();
    for (const auto& [key, entry] : entries_) {
        files.push_back(Json{
            {"path", key},
            {"lastUsed", entry.lastUsed},
        });
    }
    return Json{{"version", 1}, {"files", std::move(files)}}.dump(2);
}

RecentFilesStore RecentFilesStore::FromJson(std::string_view json) {
    RecentFilesStore store;
    try {
        const Json parsed = Json::parse(json);
        for (const Json& file : parsed.value("files", Json::array())) {
            if (!file.is_object() || !file.contains("path") || !file["path"].is_string()) {
                continue; // one malformed entry shouldn't discard the rest
            }
            Entry entry;
            entry.lastUsed = file.value("lastUsed", std::int64_t{0});
            // Stored keys were normalized at Record time; inserted verbatim,
            // same reasoning as FilePlaceStore::FromJson.
            store.entries_.emplace(file["path"].get<std::string>(), entry);
        }
        store.EvictPastCap();
        store.dirty_ = false; // freshly loaded content is by definition in sync
    }
    catch (const std::exception&) {
        return RecentFilesStore{}; // malformed -> empty, by contract
    }
    return store;
}

std::size_t RecentFilesStore::Count() const {
    return entries_.size();
}

bool RecentFilesStore::Dirty() const {
    return dirty_;
}

void RecentFilesStore::ClearDirty() {
    dirty_ = false;
}

// -- Process-wide store + toggle ----------------------------------------------

void SetRecentFilesEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(RecentFilesMutex());
    RecentFilesEnabledStorage() = enabled;
}

bool RecentFilesEnabled() {
    const std::lock_guard<std::mutex> lock(RecentFilesMutex());
    return RecentFilesEnabledStorage();
}

std::filesystem::path RecentFilesPath() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "recent-files.json";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "recent-files.json";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

void LoadRecentFiles() {
    try {
        const std::filesystem::path       path = RecentFilesPath();
        const std::lock_guard<std::mutex> lock(RecentFilesMutex());
        StoreStorage().LoadFromFile(path);
    }
    catch (const std::exception&) {
        // Swallowed -- see this function's own doc comment.
    }
}

void SaveRecentFiles(bool force) {
    try {
        const std::filesystem::path       path = RecentFilesPath();
        const std::lock_guard<std::mutex> lock(RecentFilesMutex());
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

void RecordRecentFile(const text::Buffer& buffer) {
    if (!buffer.Path() || buffer.IsLoading()) {
        return;
    }

    const std::lock_guard<std::mutex> lock(RecentFilesMutex());
    if (!RecentFilesEnabledStorage()) {
        return;
    }
    StoreStorage().Record(*buffer.Path());
}

std::vector<std::string> RecentFilePaths() {
    const std::lock_guard<std::mutex> lock(RecentFilesMutex());
    if (!RecentFilesEnabledStorage()) {
        return {};
    }
    return StoreStorage().Paths();
}

void ResetRecentFilesForTesting() {
    const std::lock_guard<std::mutex> lock(RecentFilesMutex());
    StoreStorage()              = RecentFilesStore{};
    RecentFilesEnabledStorage() = true;
}

} // namespace ned::editor
