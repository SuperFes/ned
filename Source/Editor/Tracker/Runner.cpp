#include "Runner.h"

#include <utility>

#include "Editor/DiagnosticsLog.h"
#include "Registry.h"

namespace ned::editor::tracker {

namespace {

    std::string FailureDetail(const std::string& panelName, std::optional<int> exitCode, const std::string& output) {
        std::string detail = "tracker panel \"" + panelName + "\": fetch failed";
        if (!exitCode) {
            detail += output.empty() ? " (couldn't start the command)" : " (terminated)";
        }
        else {
            detail += " (exit " + std::to_string(*exitCode) + ")";
        }
        if (!output.empty()) {
            constexpr std::size_t kMaxDetailLength = 200;
            std::string           trimmed          = output.substr(0, kMaxDetailLength);
            while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
                trimmed.pop_back();
            }
            detail += ": " + trimmed;
        }
        return detail;
    }

} // namespace

std::expected<std::vector<Issue>, std::string> FinishList(const Provider& provider, const std::string& panelName,
                                                          const std::string& output, std::optional<int> exitCode) {
    if (!exitCode || *exitCode != 0) {
        return std::unexpected(FailureDetail(panelName, exitCode, output));
    }
    try {
        return provider.ParseList(output);
    }
    catch (const std::exception& e) {
        return std::unexpected("tracker panel \"" + panelName + "\": " + e.what());
    }
}

Runner::Runner(ned::ui::EventLoop& eventLoop) : eventLoop_(eventLoop) {
}

bool Runner::IsFetching(const std::string& panelName) const {
    return running_.contains(panelName);
}

void Runner::RequestIssues(const std::string& panelName, std::function<void(std::vector<Issue>)> onComplete,
                           std::function<void(std::string)> onError) {
    const std::optional<Panel> panel = FindPanel(panelName);
    if (!panel) {
        onError("no tracker panel named \"" + panelName + "\"");
        return;
    }
    const std::optional<Connection> connection = FindConnection(panel->connection);
    if (!connection) {
        onError("tracker panel \"" + panelName + "\": no connection named \"" + panel->connection + "\"");
        return;
    }
    std::shared_ptr<const Provider> provider = FindProvider(connection->provider);
    if (!provider) {
        onError("tracker panel \"" + panelName + "\": no tracker provider named \"" + connection->provider + "\"");
        return;
    }
    if (IsFetching(panelName)) {
        onError("tracker panel \"" + panelName + "\" is already fetching");
        return;
    }

    CommandSpec spec;
    try {
        spec = provider->ListArgv(*connection, panel->query);
    }
    catch (const std::exception& e) {
        onError("tracker panel \"" + panelName + "\": " + e.what());
        return;
    }

    auto output = std::make_shared<std::string>();
    try {
        running_[panelName] = std::make_unique<tasks::TaskProcess>(
            spec.argv, eventLoop_, [output](std::string_view chunk) { output->append(chunk); },
            [this, panelName, output, provider = std::move(provider), onComplete = std::move(onComplete),
             onError](std::optional<int> exitCode) {
                auto result = FinishList(*provider, panelName, *output, exitCode);
                if (result) {
                    onComplete(std::move(*result));
                }
                else {
                    onError(std::move(result.error()));
                }
                // Destroys the TaskProcess that owns this closure (and panelName
                // with it), so it must come last and must not key on a capture.
                const std::string key = panelName;
                running_.erase(key);
            });
    }
    catch (const std::exception& e) {
        running_.erase(panelName);
        LogMessage(LogCategory::Tracker, LogSeverity::Error, "tracker panel \"" + panelName + "\": " + e.what());
        onError(FailureDetail(panelName, std::nullopt, {}));
    }
}

} // namespace ned::editor::tracker
