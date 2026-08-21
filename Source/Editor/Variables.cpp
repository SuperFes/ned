#include "Variables.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace ned::editor {

namespace {
    using Json = nlohmann::json;
}

std::optional<std::string> VariableStore::Get(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void VariableStore::Set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

void VariableStore::LoadFromFile(const std::filesystem::path& path) {
    values_.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return; // missing/unreadable -> empty store, by contract
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    *this = FromJson(content);
}

void VariableStore::SaveToFile(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());

    const std::filesystem::path temporary = path.string() + ".ned-tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot write variables to \"" + temporary.string() + "\"");
        }
        file << ToJson();
        if (!file.flush()) {
            throw std::runtime_error("ned: failed writing variables to \"" + temporary.string() + "\"");
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string VariableStore::ToJson() const {
    Json variables = Json::object();
    for (const auto& [key, value] : values_) {
        variables[key] = value;
    }
    return Json{{"variables", variables}}.dump(2);
}

VariableStore VariableStore::FromJson(std::string_view json) {
    VariableStore store;
    try {
        const Json parsed = Json::parse(json);
        for (const auto& [key, value] : parsed.at("variables").items()) {
            if (value.is_string()) {
                store.values_[key] = value.get<std::string>();
            }
        }
    }
    catch (const std::exception&) {
        return VariableStore{}; // malformed -> empty, by contract
    }
    return store;
}

std::size_t VariableStore::Count() const {
    return values_.size();
}

// -- Process-wide store -------------------------------------------------------

namespace {

    std::mutex& VariablesMutex() {
        static std::mutex mutex;
        return mutex;
    }

    VariableStore& StoreStorage() {
        static VariableStore store;
        return store;
    }

} // namespace

std::filesystem::path VariablesPath() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "variables.json";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "variables.json";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

void LoadVariables() {
    try {
        const std::filesystem::path       path = VariablesPath();
        const std::lock_guard<std::mutex> lock(VariablesMutex());
        StoreStorage().LoadFromFile(path);
    }
    catch (const std::exception&) {
        // Swallowed -- see the header comment.
    }
}

std::optional<std::string> Variable(const std::string& key) {
    const std::lock_guard<std::mutex> lock(VariablesMutex());
    return StoreStorage().Get(key);
}

void SetVariable(const std::string& key, const std::string& value) {
    const std::lock_guard<std::mutex> lock(VariablesMutex());
    StoreStorage().Set(key, value);
    try {
        StoreStorage().SaveToFile(VariablesPath());
    }
    catch (const std::exception&) {
        // Swallowed -- the in-memory Set above always takes effect for the
        // rest of this run; only the persistence is best-effort. See the
        // header comment.
    }
}

} // namespace ned::editor
