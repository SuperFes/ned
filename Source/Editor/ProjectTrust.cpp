#include "ProjectTrust.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "Session.h"

namespace ned::editor {

namespace {

    using Json = nlohmann::json;

    constexpr int          kDefaultExpiryDays = 30;
    constexpr std::int64_t kDaySeconds        = 24 * 60 * 60;

    std::int64_t NowSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    // Same FNV-1a 64 as ProjectSession.cpp's session-filename hash --
    // duplicated for the same "not worth a shared dependency" reason.
    std::string Fnv1a64Hex(std::string_view key) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const unsigned char byte : key) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        char buffer[17];
        std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
        return buffer;
    }

    std::mutex& TrustMutex() {
        static std::mutex mutex;
        return mutex;
    }

    int& ExpiryDaysStorage() {
        static int days = kDefaultExpiryDays;
        return days;
    }

    ProjectTrustStore& StoreStorage() {
        static ProjectTrustStore store;
        return store;
    }

} // namespace

bool ProjectTrustStore::IsTrusted(const std::filesystem::path& initFile, std::string_view contentHash) const {
    const auto it = entries_.find(FilePlaceStore::NormalizePathKey(initFile));
    return it != entries_.end() && it->second.contentHash == contentHash;
}

void ProjectTrustStore::Trust(const std::filesystem::path& initFile, std::string contentHash,
                              std::optional<std::int64_t> nowSeconds) {
    const std::int64_t now   = nowSeconds.value_or(NowSeconds());
    ProjectTrustEntry& entry = entries_[FilePlaceStore::NormalizePathKey(initFile)];
    entry.contentHash        = std::move(contentHash);
    entry.trustedAt          = now;
    entry.lastUsed           = now;
}

void ProjectTrustStore::Touch(const std::filesystem::path& initFile, std::optional<std::int64_t> nowSeconds) {
    const auto it = entries_.find(FilePlaceStore::NormalizePathKey(initFile));
    if (it != entries_.end()) {
        it->second.lastUsed = nowSeconds.value_or(NowSeconds());
    }
}

void ProjectTrustStore::PruneExpired(std::int64_t nowSeconds, int expiryDays) {
    if (expiryDays <= 0) {
        return; // never expire
    }
    const std::int64_t cutoff = nowSeconds - static_cast<std::int64_t>(expiryDays) * kDaySeconds;
    for (auto it = entries_.begin(); it != entries_.end();) {
        it = it->second.lastUsed < cutoff ? entries_.erase(it) : std::next(it);
    }
}

void ProjectTrustStore::PruneMissingFiles() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        std::error_code ec;
        it = std::filesystem::exists(it->first, ec) ? std::next(it) : entries_.erase(it);
    }
}

void ProjectTrustStore::LoadFromFile(const std::filesystem::path& path) {
    entries_.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    *this = FromJson(content);
}

void ProjectTrustStore::SaveToFile(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());

    const std::filesystem::path temporary = path.string() + ".ned-tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot write trust store to \"" + temporary.string() + "\"");
        }
        file << ToJson();
        if (!file.flush()) {
            throw std::runtime_error("ned: failed writing trust store to \"" + temporary.string() + "\"");
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string ProjectTrustStore::ToJson() const {
    Json trusted = Json::array();
    for (const auto& [key, entry] : entries_) {
        trusted.push_back({
            {"path", key},
            {"hash", entry.contentHash},
            {"trustedAt", entry.trustedAt},
            {"lastUsed", entry.lastUsed},
        });
    }
    return Json{{"version", 1}, {"trusted", std::move(trusted)}}.dump(2);
}

ProjectTrustStore ProjectTrustStore::FromJson(std::string_view json) {
    ProjectTrustStore store;
    try {
        const Json parsed = Json::parse(json);
        for (const Json& item : parsed.value("trusted", Json::array())) {
            if (!item.is_object() || !item.contains("path") || !item["path"].is_string() ||
                !item.contains("hash") || !item["hash"].is_string()) {
                continue; // one malformed entry shouldn't discard the rest
            }
            ProjectTrustEntry entry;
            entry.contentHash = item["hash"].get<std::string>();
            entry.trustedAt   = item.value("trustedAt", std::int64_t{0});
            entry.lastUsed    = item.value("lastUsed", std::int64_t{0});
            store.entries_.emplace(item["path"].get<std::string>(), std::move(entry));
        }
    }
    catch (const std::exception&) {
        return ProjectTrustStore{};
    }
    return store;
}

std::size_t ProjectTrustStore::Count() const {
    return entries_.size();
}

std::optional<ProjectTrustEntry> ProjectTrustStore::Lookup(const std::filesystem::path& initFile) const {
    const auto it = entries_.find(FilePlaceStore::NormalizePathKey(initFile));
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string> HashFileContent(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!file.good() && !file.eof()) {
        return std::nullopt;
    }
    return Fnv1a64Hex(content);
}

// -- Process-wide store + settings --------------------------------------------

void SetProjectTrustExpiryDays(int days) {
    const std::lock_guard<std::mutex> lock(TrustMutex());
    ExpiryDaysStorage() = days;
}

int ProjectTrustExpiryDays() {
    const std::lock_guard<std::mutex> lock(TrustMutex());
    return ExpiryDaysStorage();
}

std::filesystem::path TrustedFilePath() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "trusted.json";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "trusted.json";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

void LoadProjectTrust() {
    try {
        const std::filesystem::path       path = TrustedFilePath();
        const std::lock_guard<std::mutex> lock(TrustMutex());
        StoreStorage().LoadFromFile(path);
        StoreStorage().PruneExpired(NowSeconds(), ExpiryDaysStorage());
        StoreStorage().PruneMissingFiles();
    }
    catch (const std::exception&) {
        // Swallowed -- see the header comment.
    }
}

void SaveProjectTrust() {
    try {
        const std::filesystem::path       path = TrustedFilePath();
        const std::lock_guard<std::mutex> lock(TrustMutex());
        StoreStorage().SaveToFile(path);
    }
    catch (const std::exception&) {
        // Swallowed -- see the header comment.
    }
}

bool IsProjectInitTrusted(const std::filesystem::path& initFile, std::string_view contentHash) {
    const std::lock_guard<std::mutex> lock(TrustMutex());
    return StoreStorage().IsTrusted(initFile, contentHash);
}

void RecordProjectInitTrust(const std::filesystem::path& initFile, std::string contentHash) {
    {
        const std::lock_guard<std::mutex> lock(TrustMutex());
        StoreStorage().Trust(initFile, std::move(contentHash));
    }
    SaveProjectTrust();
}

void TouchProjectTrust(const std::filesystem::path& initFile) {
    const std::lock_guard<std::mutex> lock(TrustMutex());
    StoreStorage().Touch(initFile);
}

void ResetProjectTrustForTesting() {
    const std::lock_guard<std::mutex> lock(TrustMutex());
    StoreStorage()      = ProjectTrustStore{};
    ExpiryDaysStorage() = kDefaultExpiryDays;
}

} // namespace ned::editor
