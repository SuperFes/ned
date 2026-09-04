#include "NodeModules.h"

#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace ned::editor {

namespace {

    // Recursively narrows an "exports" field value down to a single relative
    // path string: a string is returned as-is; an object is tried against
    // Node's own condition-preference order (bounded to two levels deep --
    // real package.json condition maps are never deeper than that in
    // practice, and this is a best-effort resolver, not a spec-exhaustive
    // one). Any other shape (an array, i.e. subpath-pattern exports) yields
    // nullopt -- deliberately not attempted here.
    std::optional<std::string> PreferredExportString(const nlohmann::json& node, int depth = 0) {
        if (node.is_string()) {
            return node.get<std::string>();
        }
        if (!node.is_object() || depth >= 2) {
            return std::nullopt;
        }
        for (const char* condition : {"import", "node", "default", "require"}) {
            if (const auto it = node.find(condition); it != node.end()) {
                if (auto resolved = PreferredExportString(*it, depth + 1)) {
                    return resolved;
                }
            }
        }
        return std::nullopt;
    }

} // namespace

std::vector<std::filesystem::path> NodeModulesSearchPaths(const std::filesystem::path& baseDirectory,
                                                           const std::filesystem::path& projectRoot) {
    std::error_code errorCode;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(projectRoot, errorCode);

    std::vector<std::filesystem::path> searchPaths;
    std::filesystem::path              dir = std::filesystem::weakly_canonical(baseDirectory, errorCode);
    if (dir.empty()) {
        dir = baseDirectory;
    }

    while (true) {
        std::error_code existsError;
        if (const std::filesystem::path candidate = dir / "node_modules";
            std::filesystem::is_directory(candidate, existsError)) {
            searchPaths.push_back(candidate);
        }

        if (!errorCode && !canonicalRoot.empty() && dir == canonicalRoot) {
            break;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break; // reached the filesystem root
        }
        dir = parent;
    }

    return searchPaths;
}

std::optional<std::filesystem::path>
ResolvePackageEntryPoint(const std::filesystem::path& packageDirectory, const std::vector<std::string>& candidateExtensions) {
    std::ifstream packageJsonFile(packageDirectory / "package.json");
    if (!packageJsonFile) {
        return std::nullopt;
    }
    nlohmann::json root;
    try {
        packageJsonFile >> root;
    }
    catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    if (!root.is_object()) {
        return std::nullopt;
    }

    std::optional<std::string> relativeEntry;
    if (const auto exports = root.find("exports"); exports != root.end()) {
        const nlohmann::json& target =
            (exports->is_object() && exports->contains(".")) ? (*exports)["."] : *exports;
        relativeEntry = PreferredExportString(target);
    }
    if (!relativeEntry) {
        if (const auto main = root.find("main"); main != root.end() && main->is_string()) {
            relativeEntry = main->get<std::string>();
        }
    }
    if (!relativeEntry || relativeEntry->empty()) {
        return std::nullopt;
    }

    std::string cleaned = *relativeEntry;
    if (cleaned.rfind("./", 0) == 0) {
        cleaned = cleaned.substr(2);
    }
    const std::filesystem::path candidate = packageDirectory / cleaned;
    std::error_code             existsError;
    if (std::filesystem::exists(candidate, existsError) && !std::filesystem::is_directory(candidate, existsError)) {
        return candidate;
    }
    for (const std::string& extension : candidateExtensions) {
        std::filesystem::path withExtension = candidate;
        withExtension += ("." + extension);
        if (std::filesystem::exists(withExtension, existsError)) {
            return withExtension;
        }
    }
    if (std::filesystem::is_directory(candidate, existsError)) {
        for (const std::string& extension : candidateExtensions) {
            if (const std::filesystem::path indexFile = candidate / ("index." + extension);
                std::filesystem::exists(indexFile, existsError)) {
                return indexFile;
            }
        }
    }
    return std::nullopt;
}

} // namespace ned::editor
