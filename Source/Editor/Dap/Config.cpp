#include "Config.h"

#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ned::editor::dap {

namespace {

    std::mutex                                                g_mutex;
    std::unordered_map<std::string, std::vector<std::string>> g_adapterCommands;
    std::unordered_map<std::string, std::string>              g_launchConfigs;
    std::unordered_map<std::string, std::string>              g_attachConfigs;

} // namespace

void SetAdapterCommand(const std::string& language, std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (argv.empty()) {
        g_adapterCommands.erase(language);
    }
    else {
        g_adapterCommands[language] = std::move(argv);
    }
}

std::optional<std::vector<std::string>> AdapterCommand(const std::string& language) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_adapterCommands.find(language);
    if (it == g_adapterCommands.end()) {
        return std::nullopt;
    }
    return it->second;
}

void SetLaunchConfig(const std::string& language, std::string launchConfigJson) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (launchConfigJson.empty()) {
        g_launchConfigs.erase(language);
    }
    else {
        g_launchConfigs[language] = std::move(launchConfigJson);
    }
}

std::optional<std::string> LaunchConfig(const std::string& language) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_launchConfigs.find(language);
    if (it == g_launchConfigs.end()) {
        return std::nullopt;
    }
    return it->second;
}

void SetAttachConfig(const std::string& language, std::string attachConfigJson) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (attachConfigJson.empty()) {
        g_attachConfigs.erase(language);
    }
    else {
        g_attachConfigs[language] = std::move(attachConfigJson);
    }
}

std::optional<std::string> AttachConfig(const std::string& language) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_attachConfigs.find(language);
    if (it == g_attachConfigs.end()) {
        return std::nullopt;
    }
    return it->second;
}

namespace {

    void ReplaceWorkspaceFolder(nlohmann::json& value, const std::string& root) {
        static constexpr std::string_view kVariable = "${workspaceFolder}";
        if (value.is_string()) {
            std::string text = value.get<std::string>();
            for (std::size_t at = text.find(kVariable); at != std::string::npos; at = text.find(kVariable, at + root.size())) {
                text.replace(at, kVariable.size(), root);
            }
            value = std::move(text);
            return;
        }
        if (value.is_structured()) {
            for (nlohmann::json& element : value) {
                ReplaceWorkspaceFolder(element, root);
            }
        }
    }

} // namespace

std::optional<nlohmann::json> SessionArguments(const ProjectSettings& settings, const std::string& language, bool attach, const std::filesystem::path& root) {
    const auto&                   project = attach ? settings.dapAttachByLanguage : settings.dapLaunchByLanguage;
    std::optional<nlohmann::json> arguments;
    if (const auto it = project.find(language); it != project.end()) {
        arguments = it->second;
    }
    else if (const std::optional<std::string> configured = attach ? AttachConfig(language) : LaunchConfig(language)) {
        try {
            arguments = nlohmann::json::parse(*configured);
        }
        catch (const nlohmann::json::exception& e) {
            throw std::runtime_error(std::string(attach ? "attach" : "launch") + " configuration is not valid JSON: " + e.what());
        }
    }
    if (arguments) {
        ReplaceWorkspaceFolder(*arguments, root.string());
    }
    return arguments;
}

} // namespace ned::editor::dap
