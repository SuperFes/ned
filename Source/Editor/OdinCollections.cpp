#include "OdinCollections.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <system_error>

#include <nlohmann/json.hpp>

#include "Editor/Process/ChildProcess.h"

namespace ned::editor {

namespace {

    constexpr std::array<std::string_view, 4> kToolchainCollections = {"base", "core", "vendor", "shared"};

    std::optional<std::filesystem::path> OlsCollection(const std::filesystem::path& olsJson, std::string_view name) {
        std::ifstream        in(olsJson);
        const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
        if (!config.is_object() || !config.contains("collections") || !config["collections"].is_array()) {
            return std::nullopt;
        }
        for (const nlohmann::json& collection : config["collections"]) {
            if (collection.is_object() && collection.value("name", "") == name) {
                std::filesystem::path path(collection.value("path", ""));
                return path.is_relative() ? (olsJson.parent_path() / path).lexically_normal() : path;
            }
        }
        return std::nullopt;
    }

} // namespace

std::optional<std::string> OdinCollectionName(std::string_view importPath) {
    const std::size_t colon = importPath.find(':');
    if (colon == std::string_view::npos || colon == 0) {
        return std::nullopt;
    }
    return std::string(importPath.substr(0, colon));
}

std::filesystem::path OdinCollectionRoot(std::string_view name, const std::filesystem::path& searchStart,
                                         const std::filesystem::path& odinRoot) {
    std::error_code ec;
    for (std::filesystem::path dir = searchStart; !dir.empty(); dir = dir.parent_path()) {
        if (const std::filesystem::path ols = dir / "ols.json"; std::filesystem::is_regular_file(ols, ec)) {
            if (auto path = OlsCollection(ols, name)) {
                return *path;
            }
            break;
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    for (const std::string_view builtin : kToolchainCollections) {
        if (builtin == name && !odinRoot.empty()) {
            return odinRoot / builtin;
        }
    }
    return {};
}

std::filesystem::path OdinRoot() {
    if (const char* root = std::getenv("ODIN_ROOT"); root != nullptr && *root != '\0') {
        return root;
    }
    static std::mutex                           mutex;
    static std::optional<std::filesystem::path> asked;
    const std::lock_guard<std::mutex>           lock(mutex);
    if (asked) {
        return *asked;
    }
    asked.emplace();
    if (!process::ResolveExecutable("odin")) {
        return *asked;
    }
    try {
        process::ChildProcess proc({"odin", "root"});
        std::string           output;
        while (const std::optional<std::string> chunk = proc.ReadSome(std::chrono::milliseconds(2000))) {
            if (chunk->empty()) {
                break;
            }
            output += *chunk;
        }
        proc.Kill();
        proc.WaitForExit();
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
            output.pop_back();
        }
        *asked = output;
    }
    catch (const std::exception&) {
        // No toolchain answer: collections outside ols.json stay unresolved.
    }
    return *asked;
}

} // namespace ned::editor
