#include "ProjectSettings.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace ned::editor {

ProjectSettings LoadProjectSettings(const std::filesystem::path& root) {
    ProjectSettings settings;

    std::ifstream file(root / ".ned" / "settings.json", std::ios::binary);
    if (!file) {
        return settings;
    }

    try {
        const nlohmann::json parsed = nlohmann::json::parse(file);
        if (parsed.contains("includePaths") && parsed["includePaths"].is_object()) {
            for (const auto& [modeName, entries] : parsed["includePaths"].items()) {
                if (!entries.is_array()) {
                    continue;
                }
                std::vector<std::filesystem::path> paths;
                for (const nlohmann::json& entry : entries) {
                    if (!entry.is_string()) {
                        continue;
                    }
                    const std::filesystem::path path(entry.get<std::string>());
                    paths.push_back(path.is_absolute() ? path : root / path);
                }
                settings.includePathsByMode.emplace(modeName, std::move(paths));
            }
        }
        if (parsed.contains("lspInitializationOptions") && parsed["lspInitializationOptions"].is_object()) {
            for (const auto& [language, options] : parsed["lspInitializationOptions"].items()) {
                settings.lspInitializationOptionsByLanguage.emplace(language, options);
            }
        }
        if (parsed.contains("lspWorkspaceConfiguration") && parsed["lspWorkspaceConfiguration"].is_object()) {
            settings.lspWorkspaceConfiguration = parsed["lspWorkspaceConfiguration"];
        }
        if (parsed.contains("importResolution") && parsed["importResolution"].is_object()) {
            for (const auto& [language, entry] : parsed["importResolution"].items()) {
                if (!entry.is_object()) {
                    continue;
                }
                ImportResolutionOverride override;
                if (entry.contains("extensions") && entry["extensions"].is_array()) {
                    for (const nlohmann::json& extension : entry["extensions"]) {
                        if (extension.is_string()) {
                            override.extensions.push_back(extension.get<std::string>());
                        }
                    }
                }
                if (entry.contains("indexBasenames") && entry["indexBasenames"].is_array()) {
                    for (const nlohmann::json& basename : entry["indexBasenames"]) {
                        if (basename.is_string()) {
                            override.indexBasenames.push_back(basename.get<std::string>());
                        }
                    }
                }
                if (entry.contains("searchPackageDirs") && entry["searchPackageDirs"].is_boolean()) {
                    override.searchPackageDirs = entry["searchPackageDirs"].get<bool>();
                }
                settings.importResolutionByLanguage.emplace(language, std::move(override));
            }
        }
    }
    catch (const std::exception&) {
        return ProjectSettings{}; // malformed -> nothing configured, same contract as GitIgnoreMatcher
    }

    return settings;
}

const std::vector<std::filesystem::path>& IncludePathsForMode(const ProjectSettings& settings, const std::string& modeName) {
    static const std::vector<std::filesystem::path> kEmpty;
    const auto                                       it = settings.includePathsByMode.find(modeName);
    return it != settings.includePathsByMode.end() ? it->second : kEmpty;
}

const nlohmann::json& LspInitializationOptionsForLanguage(const ProjectSettings& settings, const std::string& language) {
    static const nlohmann::json kEmpty = nlohmann::json::object();
    const auto                  it     = settings.lspInitializationOptionsByLanguage.find(language);
    return it != settings.lspInitializationOptionsByLanguage.end() ? it->second : kEmpty;
}

const ImportResolutionOverride& ImportResolutionOverrideForLanguage(const ProjectSettings& settings, const std::string& language) {
    static const ImportResolutionOverride kEmpty;
    const auto                            it = settings.importResolutionByLanguage.find(language);
    return it != settings.importResolutionByLanguage.end() ? it->second : kEmpty;
}

} // namespace ned::editor
