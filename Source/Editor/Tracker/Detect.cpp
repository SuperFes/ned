#include "Detect.h"

#include <algorithm>
#include <exception>

#include "Editor/DiagnosticsLog.h"
#include "Editor/Process/ChildProcess.h"
#include "Registry.h"

namespace ned::editor::tracker {

std::vector<std::string> ParseRemoteUrls(std::string_view remoteOutput) {
    std::vector<std::string> urls;
    while (!remoteOutput.empty()) {
        const std::size_t newline = remoteOutput.find('\n');
        std::string_view  line    = remoteOutput.substr(0, newline);
        remoteOutput.remove_prefix(newline == std::string_view::npos ? remoteOutput.size() : newline + 1);

        // "name<TAB>url (fetch)"
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            continue;
        }
        line.remove_prefix(tab + 1);
        if (const std::size_t paren = line.rfind(" ("); paren != std::string_view::npos && line.ends_with(')')) {
            line = line.substr(0, paren);
        }
        if (!line.empty() && std::ranges::find(urls, line) == urls.end()) {
            urls.emplace_back(line);
        }
    }
    return urls;
}

std::vector<std::string> GitRemoteUrls(const std::filesystem::path& root) {
    // `remote -v` rather than reading config: it applies url.insteadOf.
    const auto output = process::RunCapturingStdout({"git", "-C", root.string(), "remote", "-v"});
    return output ? ParseRemoteUrls(*output) : std::vector<std::string>{};
}

std::vector<std::string> AddDetectedPanels(const std::vector<std::string>& remoteUrls) {
    std::vector<std::string> added;
    if (remoteUrls.empty()) {
        return added;
    }
    for (const std::shared_ptr<const Provider>& provider : Providers()) {
        std::vector<Detected> detected;
        try {
            detected = provider->Detect(remoteUrls);
        }
        catch (const std::exception& e) {
            LogMessage(LogCategory::Tracker, LogSeverity::Error, std::string("tracker detection: ") + e.what());
            continue;
        }
        for (Detected& found : detected) {
            if (found.connection.name.empty() || FindConnection(found.connection.name)) {
                continue;
            }
            const std::string connectionName = found.connection.name;
            SetConnection(std::move(found.connection));
            for (Panel& panel : found.panels) {
                if (panel.name.empty() || FindPanel(panel.name)) {
                    continue;
                }
                panel.connection = connectionName;
                added.push_back(panel.name);
                AddPanel(std::move(panel));
            }
        }
    }
    return added;
}

} // namespace ned::editor::tracker
