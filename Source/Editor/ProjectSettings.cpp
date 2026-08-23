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

} // namespace ned::editor
