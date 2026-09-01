#include "ProjectRegistry.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "Session.h"

namespace ned::editor {

namespace {

    using Json = nlohmann::json;

    std::int64_t NowSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    std::mutex& RegistryMutex() {
        static std::mutex mutex;
        return mutex;
    }

    ProjectRegistryStore& StoreStorage() {
        static ProjectRegistryStore store;
        return store;
    }

} // namespace

bool ProjectRegistryStore::Add(std::string name, const std::filesystem::path& root,
                               std::optional<std::int64_t> nowSeconds) {
    const std::int64_t    now      = nowSeconds.value_or(NowSeconds());
    const std::string     key      = FilePlaceStore::NormalizePathKey(root);
    const bool            isNewOne = entries_.find(key) == entries_.end();
    ProjectRegistryEntry& entry    = entries_[key];
    entry.name                     = std::move(name);
    entry.root                     = root.string();
    entry.lastUsed                 = now;
    return isNewOne;
}

bool ProjectRegistryStore::Remove(const std::filesystem::path& root) {
    return entries_.erase(FilePlaceStore::NormalizePathKey(root)) > 0;
}

bool ProjectRegistryStore::Rename(const std::filesystem::path& root, std::string newName) {
    const auto it = entries_.find(FilePlaceStore::NormalizePathKey(root));
    if (it == entries_.end()) {
        return false;
    }
    it->second.name = std::move(newName);
    return true;
}

void ProjectRegistryStore::Touch(const std::filesystem::path& root, std::optional<std::int64_t> nowSeconds) {
    const auto it = entries_.find(FilePlaceStore::NormalizePathKey(root));
    if (it != entries_.end()) {
        it->second.lastUsed = nowSeconds.value_or(NowSeconds());
    }
}

std::optional<ProjectRegistryEntry> ProjectRegistryStore::LookupByRoot(const std::filesystem::path& root) const {
    const auto it = entries_.find(FilePlaceStore::NormalizePathKey(root));
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ProjectRegistryEntry> ProjectRegistryStore::List() const {
    std::vector<ProjectRegistryEntry> result;
    result.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        result.push_back(entry);
    }
    std::sort(result.begin(), result.end(),
              [](const ProjectRegistryEntry& a, const ProjectRegistryEntry& b) { return a.lastUsed > b.lastUsed; });
    return result;
}

void ProjectRegistryStore::LoadFromFile(const std::filesystem::path& path) {
    entries_.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    *this = FromJson(content);
}

void ProjectRegistryStore::SaveToFile(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());

    const std::filesystem::path temporary = path.string() + ".ned-tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot write project registry to \"" + temporary.string() + "\"");
        }
        file << ToJson();
        if (!file.flush()) {
            throw std::runtime_error("ned: failed writing project registry to \"" + temporary.string() + "\"");
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string ProjectRegistryStore::ToJson() const {
    Json projects = Json::array();
    for (const auto& [key, entry] : entries_) {
        projects.push_back({
            {"root", key},
            {"name", entry.name},
            {"lastUsed", entry.lastUsed},
        });
    }
    return Json{{"version", 1}, {"projects", std::move(projects)}}.dump(2);
}

ProjectRegistryStore ProjectRegistryStore::FromJson(std::string_view json) {
    ProjectRegistryStore store;
    try {
        const Json parsed = Json::parse(json);
        for (const Json& item : parsed.value("projects", Json::array())) {
            if (!item.is_object() || !item.contains("root") || !item["root"].is_string() ||
                !item.contains("name") || !item["name"].is_string()) {
                continue; // one malformed entry shouldn't discard the rest
            }
            ProjectRegistryEntry entry;
            entry.root     = item["root"].get<std::string>();
            entry.name     = item["name"].get<std::string>();
            entry.lastUsed = item.value("lastUsed", std::int64_t{0});
            store.entries_.emplace(item["root"].get<std::string>(), std::move(entry));
        }
    }
    catch (const std::exception&) {
        return ProjectRegistryStore{};
    }
    return store;
}

std::size_t ProjectRegistryStore::Count() const {
    return entries_.size();
}

// -- Process-wide store --------------------------------------------------------

std::filesystem::path ProjectRegistryPath() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "projects.json";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "projects.json";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

void LoadProjectRegistry() {
    try {
        const std::filesystem::path       path = ProjectRegistryPath();
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        StoreStorage().LoadFromFile(path);
    }
    catch (const std::exception&) {
        // Swallowed -- see the header comment on the sibling stores.
    }
}

void SaveProjectRegistry() {
    try {
        const std::filesystem::path       path = ProjectRegistryPath();
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        StoreStorage().SaveToFile(path);
    }
    catch (const std::exception&) {
        // Swallowed -- see the header comment on the sibling stores.
    }
}

bool RegisterProject(std::string name, const std::filesystem::path& root) {
    bool isNewOne = false;
    {
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        isNewOne = StoreStorage().Add(std::move(name), root);
    }
    SaveProjectRegistry();
    return isNewOne;
}

bool UnregisterProject(const std::filesystem::path& root) {
    bool removed = false;
    {
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        removed = StoreStorage().Remove(root);
    }
    SaveProjectRegistry();
    return removed;
}

bool RenameProject(const std::filesystem::path& root, std::string newName) {
    bool renamed = false;
    {
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        renamed = StoreStorage().Rename(root, std::move(newName));
    }
    SaveProjectRegistry();
    return renamed;
}

void TouchProject(const std::filesystem::path& root) {
    {
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        StoreStorage().Touch(root);
    }
    SaveProjectRegistry();
}

std::vector<ProjectRegistryEntry> ListProjects() {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    return StoreStorage().List();
}

std::optional<ProjectRegistryEntry> FindProjectByRoot(const std::filesystem::path& root) {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    return StoreStorage().LookupByRoot(root);
}

void ResetProjectRegistryForTesting() {
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    StoreStorage() = ProjectRegistryStore{};
}

} // namespace ned::editor
