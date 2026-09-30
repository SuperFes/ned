#include "Runner.h"

#include <utility>

#include "Editor/DiagnosticsLog.h"
#include "Registry.h"

namespace ned::editor::tracker {

namespace {

    std::string FailureDetail(const std::string& subject, std::optional<int> exitCode, const std::string& output) {
        std::string detail = subject + ": fetch failed";
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

    std::string PanelSubject(const std::string& panelName) {
        return "tracker panel \"" + panelName + "\"";
    }

    std::string IssueSubject(const std::string& key) {
        return "tracker issue \"" + key + "\"";
    }

    void FillEmpty(std::string& field, const std::string& fallback) {
        if (field.empty()) {
            field = fallback;
        }
    }

} // namespace

std::expected<std::vector<Issue>, std::string> FinishList(const Provider& provider, const std::string& panelName,
                                                          const std::string& output, std::optional<int> exitCode) {
    if (!exitCode || *exitCode != 0) {
        return std::unexpected(FailureDetail(PanelSubject(panelName), exitCode, output));
    }
    try {
        return provider.ParseList(output);
    }
    catch (const std::exception& e) {
        return std::unexpected(PanelSubject(panelName) + ": " + e.what());
    }
}

std::expected<IssueDetail, std::string> FinishView(const Provider& provider, const Issue& listed, const std::string& output,
                                                   std::optional<int> exitCode) {
    if (!exitCode || *exitCode != 0) {
        return std::unexpected(FailureDetail(IssueSubject(listed.key), exitCode, output));
    }
    IssueDetail detail;
    try {
        detail = provider.ParseView(output);
    }
    catch (const std::exception& e) {
        return std::unexpected(IssueSubject(listed.key) + ": " + e.what());
    }
    Issue& issue = detail.issue;
    FillEmpty(issue.key, listed.key);
    FillEmpty(issue.title, listed.title);
    FillEmpty(issue.status, listed.status);
    FillEmpty(issue.assignee, listed.assignee);
    FillEmpty(issue.url, listed.url);
    FillEmpty(issue.updated, listed.updated);
    if (issue.labels.empty()) {
        issue.labels = listed.labels;
    }
    return detail;
}

Runner::Runner(ned::ui::EventLoop& eventLoop) : eventLoop_(eventLoop) {
}

bool Runner::IsFetching(const std::string& panelName) const {
    return running_.contains(panelName);
}

std::expected<Runner::Resolved, std::string> Runner::Resolve(const std::string& panelName) const {
    const std::optional<Panel> panel = FindPanel(panelName);
    if (!panel) {
        return std::unexpected("no tracker panel named \"" + panelName + "\"");
    }
    std::optional<Connection> connection = FindConnection(panel->connection);
    if (!connection) {
        return std::unexpected(PanelSubject(panelName) + ": no connection named \"" + panel->connection + "\"");
    }
    std::shared_ptr<const Provider> provider = FindProvider(connection->provider);
    if (!provider) {
        return std::unexpected(PanelSubject(panelName) + ": no tracker provider named \"" + connection->provider + "\"");
    }
    return Resolved{.connection = std::move(*connection), .query = panel->query, .provider = std::move(provider)};
}

bool Runner::Spawn(const std::string& runKey, const std::string& subject, const std::vector<std::string>& argv,
                   std::function<void(const std::string&, std::optional<int>)> finish) {
    auto output = std::make_shared<std::string>();
    try {
        running_[runKey] = std::make_unique<tasks::TaskProcess>(
            argv, eventLoop_, [output](std::string_view chunk) { output->append(chunk); },
            [this, runKey, output, finish = std::move(finish)](std::optional<int> exitCode) {
                finish(*output, exitCode);
                // Destroys the TaskProcess that owns this closure (and runKey
                // with it), so it must come last and must not key on a capture.
                const std::string key = runKey;
                running_.erase(key);
            });
        return true;
    }
    catch (const std::exception& e) {
        running_.erase(runKey);
        LogMessage(LogCategory::Tracker, LogSeverity::Error, subject + ": " + e.what());
        return false;
    }
}

void Runner::RequestIssues(const std::string& panelName, std::function<void(std::vector<Issue>)> onComplete,
                           std::function<void(std::string)> onError) {
    auto resolved = Resolve(panelName);
    if (!resolved) {
        onError(std::move(resolved.error()));
        return;
    }
    if (IsFetching(panelName)) {
        onError(PanelSubject(panelName) + " is already fetching");
        return;
    }

    CommandSpec spec;
    try {
        spec = resolved->provider->ListArgv(resolved->connection, resolved->query);
    }
    catch (const std::exception& e) {
        onError(PanelSubject(panelName) + ": " + e.what());
        return;
    }

    const bool spawned =
        Spawn(panelName, PanelSubject(panelName), spec.argv,
              [panelName, provider = resolved->provider, onComplete, onError](const std::string& output, std::optional<int> exitCode) {
                  auto result = FinishList(*provider, panelName, output, exitCode);
                  if (!result) {
                      onError(std::move(result.error()));
                      return;
                  }
                  SetPanelIssues(panelName, *result);
                  onComplete(std::move(*result));
              });
    if (!spawned) {
        onError(FailureDetail(PanelSubject(panelName), std::nullopt, {}));
    }
}

void Runner::RequestIssue(const std::string& panelName, const Issue& listed, std::function<void(IssueDetail)> onComplete,
                          std::function<void(std::string)> onError) {
    auto resolved = Resolve(panelName);
    if (!resolved) {
        onError(std::move(resolved.error()));
        return;
    }
    // Keyed apart from list fetches, which are keyed by bare panel name.
    const std::string runKey = "issue:" + resolved->connection.name + ":" + listed.key;
    if (running_.contains(runKey)) {
        onError(IssueSubject(listed.key) + " is already fetching");
        return;
    }

    std::optional<CommandSpec> spec;
    try {
        spec = resolved->provider->ViewArgv(resolved->connection, listed.key);
    }
    catch (const std::exception& e) {
        onError(IssueSubject(listed.key) + ": " + e.what());
        return;
    }
    if (!spec) {
        onComplete(IssueDetail{.issue = listed});
        return;
    }

    const bool spawned =
        Spawn(runKey, IssueSubject(listed.key), spec->argv,
              [listed, provider = resolved->provider, onComplete, onError](const std::string& output, std::optional<int> exitCode) {
                  auto result = FinishView(*provider, listed, output, exitCode);
                  if (result) {
                      onComplete(std::move(*result));
                  }
                  else {
                      onError(std::move(result.error()));
                  }
              });
    if (!spawned) {
        onError(FailureDetail(IssueSubject(listed.key), std::nullopt, {}));
    }
}

} // namespace ned::editor::tracker
