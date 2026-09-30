#include "Runner.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "Credentials.h"
#include "Editor/DiagnosticsLog.h"
#include "PrivateFile.h"
#include "Registry.h"

namespace ned::editor::tracker {

namespace {

    std::string FailureDetail(const std::string& subject, const std::string& what, std::optional<int> exitCode, const std::string& output) {
        std::string detail = subject + ": " + what + " failed";
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

    std::string ConnectionSubject(const std::string& connectionName) {
        return "tracker connection \"" + connectionName + "\"";
    }

    // Completes "connection X can't ...".
    const char* CapabilityVerb(Capability capability) {
        switch (capability) {
            case Capability::Transition:
                return "change an issue's status";
            case Capability::Assign:
                return "assign issues";
            case Capability::Comment:
                return "comment on issues";
            case Capability::Worklog:
                return "log work";
            case Capability::ProjectKeys:
                return "list project keys";
            case Capability::Mine:
                return "list your issues";
        }
        return "do that";
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
        return std::unexpected(FailureDetail(PanelSubject(panelName), "fetch", exitCode, output));
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
        return std::unexpected(FailureDetail(IssueSubject(listed.key), "fetch", exitCode, output));
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

std::expected<std::vector<std::string>, std::string> SubstituteInputFile(std::vector<std::string> argv, const std::string& path) {
    bool used = false;
    for (std::string& arg : argv) {
        for (std::size_t at = arg.find(kInputFilePlaceholder); at != std::string::npos;
             at             = arg.find(kInputFilePlaceholder, at + path.size())) {
            arg.replace(at, kInputFilePlaceholder.size(), path);
            used = true;
        }
    }
    if (!used) {
        return std::unexpected("the command has input but no " + std::string(kInputFilePlaceholder) + " argument to read it from");
    }
    return argv;
}

std::expected<void, std::string> FinishAction(const std::string& key, const std::string& what, const std::string& output,
                                              std::optional<int> exitCode) {
    if (!exitCode || *exitCode != 0) {
        return std::unexpected(FailureDetail(IssueSubject(key), what, exitCode, output));
    }
    return {};
}

std::expected<std::vector<Choice>, std::string> FinishChoices(const Provider& provider, Capability capability, const std::string& key,
                                                              const std::string& output, std::optional<int> exitCode) {
    const std::string what = capability == Capability::Transition ? "listing statuses" : "listing assignees";
    if (!exitCode || *exitCode != 0) {
        return std::unexpected(FailureDetail(IssueSubject(key), what, exitCode, output));
    }
    try {
        return capability == Capability::Transition ? provider.ParseTransitions(output) : provider.ParseAssignees(output);
    }
    catch (const std::exception& e) {
        return std::unexpected(IssueSubject(key) + ": " + e.what());
    }
}

void PutOwnUserFirst(std::vector<Choice>& users, const std::string& email) {
    if (email.empty()) {
        return;
    }
    const auto lower = [](std::string text) {
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const auto own = std::ranges::find_if(users, [&](const Choice& user) { return !user.email.empty() && lower(user.email) == lower(email); });
    if (own == users.end()) {
        return;
    }
    own->name += " (me)";
    std::rotate(users.begin(), own, own + 1);
}

std::expected<std::vector<std::string>, std::string> FinishProjectKeys(const Provider& provider, const std::string& connectionName,
                                                                       const std::string& output, std::optional<int> exitCode) {
    if (!exitCode || *exitCode != 0) {
        return std::unexpected(FailureDetail(ConnectionSubject(connectionName), "listing project keys", exitCode, output));
    }
    try {
        return provider.ParseProjectKeys(output);
    }
    catch (const std::exception& e) {
        return std::unexpected(ConnectionSubject(connectionName) + ": " + e.what());
    }
}

std::expected<std::string, std::string> ConnectionForAction(const std::string& key, Capability capability) {
    const std::optional<std::string> connectionName = ConnectionForIssue(key);
    if (!connectionName) {
        return std::unexpected("no tracker panel has fetched " + key + " -- open it from one first");
    }
    const std::optional<Connection> connection = FindConnection(*connectionName);
    const auto                      provider   = connection ? FindProvider(connection->provider) : nullptr;
    if (!provider) {
        return std::unexpected(ConnectionSubject(*connectionName) + " has no provider");
    }
    if (!provider->Supports(capability)) {
        return std::unexpected(ConnectionSubject(*connectionName) + " can't " + CapabilityVerb(capability));
    }
    return *connectionName;
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

std::expected<Runner::Resolved, std::string> Runner::ResolveConnection(const std::string& connectionName) const {
    std::optional<Connection> connection = FindConnection(connectionName);
    if (!connection) {
        return std::unexpected("no tracker connection named \"" + connectionName + "\"");
    }
    std::shared_ptr<const Provider> provider = FindProvider(connection->provider);
    if (!provider) {
        return std::unexpected(ConnectionSubject(connectionName) + ": no tracker provider named \"" + connection->provider + "\"");
    }
    return Resolved{.connection = std::move(*connection), .query = {}, .provider = std::move(provider)};
}

std::expected<Runner::Resolved, std::string> Runner::ResolveConnection(const std::string& connectionName, Capability capability) const {
    auto resolved = ResolveConnection(connectionName);
    if (resolved && !resolved->provider->Supports(capability)) {
        return std::unexpected(ConnectionSubject(connectionName) + " can't " + CapabilityVerb(capability));
    }
    return resolved;
}

bool Runner::Spawn(const std::string& runKey, const std::string& subject, const std::vector<std::string>& argv, Finish finish,
                   process::StderrMode stderrMode) {
    auto output = std::make_shared<std::string>();
    try {
        running_[runKey] = std::make_unique<tasks::TaskProcess>(
            argv, eventLoop_, [output](std::string_view chunk) { output->append(chunk); },
            [this, runKey, output, finish = std::move(finish)](std::optional<int> exitCode) mutable {
                // Erasing destroys the TaskProcess that owns this closure, so
                // everything finish needs is moved out of it first.
                Runner&                            self      = *this;
                const std::string                  key       = runKey;
                const std::shared_ptr<std::string> collected = std::move(output);
                const Finish                       done      = std::move(finish);
                self.running_.erase(key);
                done(*collected, exitCode);
            },
            stderrMode);
        return true;
    }
    catch (const std::exception& e) {
        running_.erase(runKey);
        LogMessage(LogCategory::Tracker, LogSeverity::Error, subject + ": " + e.what());
        return false;
    }
}

void Runner::Launch(const std::string& runKey, const std::string& subject, const std::string& what, const Connection& connection,
                    const CommandSpec& spec, Finish finish, const std::function<void(std::string)>& onError) {
    std::vector<std::string> argv = spec.argv;
    if (spec.input) {
        auto file = PrivateFile::Write(*spec.input);
        if (!file) {
            onError(subject + ": " + file.error());
            return;
        }
        auto substituted = SubstituteInputFile(std::move(argv), (*file)->Path().string());
        if (!substituted) {
            onError(subject + ": " + substituted.error());
            return;
        }
        argv = std::move(*substituted);
        // Removed with the last closure that could still need it.
        finish = [file = std::move(*file), inner = std::move(finish)](const std::string& output, std::optional<int> exitCode) {
            inner(output, exitCode);
        };
    }
    if (!spec.curlCredentials) {
        if (!Spawn(runKey, subject, argv, std::move(finish))) {
            onError(FailureDetail(subject, what, std::nullopt, {}));
        }
        return;
    }
    if (auto missing = MissingCredentials(connection)) {
        onError(subject + ": " + *missing);
        return;
    }
    // The token command's stderr is dropped: nothing it prints is shown.
    const bool spawned = Spawn(
        runKey, subject, connection.tokenCommand,
        [this, runKey, subject, what, connection, argv = std::move(argv), finish = std::move(finish), onError](const std::string& output,
                                                                                                               std::optional<int> exitCode) {
            auto command = CredentialCommand(connection, argv, output, exitCode);
            if (!command) {
                onError(subject + ": " + command.error());
                return;
            }
            // The file lives as long as curl's exit closure, and goes with it.
            const bool curlSpawned = Spawn(runKey, subject, command->argv,
                                           [file = command->file, finish](const std::string& curlOutput, std::optional<int> curlExit) {
                                               finish(curlOutput, curlExit);
                                           });
            if (!curlSpawned) {
                onError(FailureDetail(subject, what, std::nullopt, {}));
            }
        },
        process::StderrMode::Discard);
    if (!spawned) {
        onError(subject + ": couldn't start the :token-command of connection \"" + connection.name + "\"");
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

    Launch(
        panelName, PanelSubject(panelName), "fetch", resolved->connection, spec,
        [panelName, provider = resolved->provider, onComplete, onError](const std::string& output, std::optional<int> exitCode) {
            auto result = FinishList(*provider, panelName, output, exitCode);
            if (!result) {
                onError(std::move(result.error()));
                return;
            }
            SetPanelIssues(panelName, *result);
            onComplete(std::move(*result));
        },
        onError);
}

void Runner::RequestIssue(const std::string& panelName, const Issue& listed, std::function<void(IssueDetail)> onComplete,
                          std::function<void(std::string)> onError) {
    auto resolved = Resolve(panelName);
    if (!resolved) {
        onError(std::move(resolved.error()));
        return;
    }
    FetchIssue(*resolved, listed, std::move(onComplete), std::move(onError));
}

void Runner::RequestIssueOn(const std::string& connectionName, const Issue& listed, std::function<void(IssueDetail)> onComplete,
                            std::function<void(std::string)> onError) {
    auto resolved = ResolveConnection(connectionName);
    if (!resolved) {
        onError(std::move(resolved.error()));
        return;
    }
    FetchIssue(*resolved, listed, std::move(onComplete), std::move(onError));
}

void Runner::FetchIssue(const Resolved& resolved, const Issue& listed, std::function<void(IssueDetail)> onComplete,
                        std::function<void(std::string)> onError) {
    // Keyed apart from list fetches, which are keyed by bare panel name.
    const std::string runKey = "issue:" + resolved.connection.name + ":" + listed.key;
    if (running_.contains(runKey)) {
        onError(IssueSubject(listed.key) + " is already fetching");
        return;
    }

    std::optional<CommandSpec> spec;
    try {
        spec = resolved.provider->ViewArgv(resolved.connection, listed.key);
    }
    catch (const std::exception& e) {
        onError(IssueSubject(listed.key) + ": " + e.what());
        return;
    }
    if (!spec) {
        onComplete(IssueDetail{.issue = listed});
        return;
    }

    Launch(
        runKey, IssueSubject(listed.key), "fetch", resolved.connection, *spec,
        [listed, provider = resolved.provider, onComplete, onError](const std::string& output, std::optional<int> exitCode) {
            auto result = FinishView(*provider, listed, output, exitCode);
            if (result) {
                onComplete(std::move(*result));
            }
            else {
                onError(std::move(result.error()));
            }
        },
        onError);
}

void Runner::Run(const std::string& runKey, const std::string& subject, const std::string& what, const std::string& connectionName,
                 Capability                                                                   capability,
                 const std::function<CommandSpec(const Provider&, const Connection&)>&        makeSpec,
                 std::function<void(const Provider&, const std::string&, std::optional<int>)> finish,
                 const std::function<void(std::string)>&                                      onError) {
    auto resolved = ResolveConnection(connectionName, capability);
    if (!resolved) {
        onError(std::move(resolved.error()));
        return;
    }
    if (running_.contains(runKey)) {
        onError(subject + " is busy with a previous request");
        return;
    }
    CommandSpec spec;
    try {
        spec = makeSpec(*resolved->provider, resolved->connection);
    }
    catch (const std::exception& e) {
        onError(subject + ": " + e.what());
        return;
    }
    Launch(
        runKey, subject, what, resolved->connection, spec,
        [provider = resolved->provider, finish = std::move(finish)](const std::string& output, std::optional<int> exitCode) {
            finish(*provider, output, exitCode);
        },
        onError);
}

void Runner::Act(const std::string& connectionName, const std::string& key, Capability capability, const std::string& what,
                 const std::function<CommandSpec(const Provider&, const Connection&)>& makeSpec, std::function<void()> onDone,
                 std::function<void(std::string)> onError) {
    Run(
        "act:" + connectionName + ":" + key, IssueSubject(key), what, connectionName, capability, makeSpec,
        [key, what, onDone = std::move(onDone), onError](const Provider&, const std::string& output, std::optional<int> exitCode) {
            if (auto result = FinishAction(key, what, output, exitCode); result) {
                onDone();
            }
            else {
                onError(std::move(result.error()));
            }
        },
        onError);
}

void Runner::RequestTransitions(const std::string& connectionName, const std::string& key,
                                std::function<void(std::vector<Choice>)> onComplete, std::function<void(std::string)> onError) {
    Run(
        "choices:" + connectionName + ":" + key, IssueSubject(key), "listing statuses", connectionName, Capability::Transition,
        [&key](const Provider& provider, const Connection& connection) { return provider.TransitionsArgv(connection, key); },
        [key, onComplete = std::move(onComplete), onError](const Provider& provider, const std::string& output, std::optional<int> exitCode) {
            auto result = FinishChoices(provider, Capability::Transition, key, output, exitCode);
            if (result) {
                onComplete(std::move(*result));
            }
            else {
                onError(std::move(result.error()));
            }
        },
        onError);
}

void Runner::RequestAssignees(const std::string& connectionName, const std::string& key,
                              std::function<void(std::vector<Choice>)> onComplete, std::function<void(std::string)> onError) {
    Run(
        "choices:" + connectionName + ":" + key, IssueSubject(key), "listing assignees", connectionName, Capability::Assign,
        [&key](const Provider& provider, const Connection& connection) { return provider.AssigneesArgv(connection, key); },
        [key, email = FindConnection(connectionName).value_or(Connection{}).email, onComplete = std::move(onComplete),
         onError](const Provider& provider, const std::string& output, std::optional<int> exitCode) {
            auto result = FinishChoices(provider, Capability::Assign, key, output, exitCode);
            if (result) {
                PutOwnUserFirst(*result, email);
                onComplete(std::move(*result));
            }
            else {
                onError(std::move(result.error()));
            }
        },
        onError);
}

void Runner::Transition(const std::string& connectionName, const std::string& key, const Choice& transition, std::function<void()> onDone,
                        std::function<void(std::string)> onError) {
    Act(
        connectionName, key, Capability::Transition, "status change",
        [&key, &transition](const Provider& provider, const Connection& connection) {
            return provider.TransitionArgv(connection, key, transition.id);
        },
        std::move(onDone), std::move(onError));
}

void Runner::Assign(const std::string& connectionName, const std::string& key, const Choice& user, std::function<void()> onDone,
                    std::function<void(std::string)> onError) {
    Act(
        connectionName, key, Capability::Assign, "assign",
        [&key, &user](const Provider& provider, const Connection& connection) { return provider.AssignArgv(connection, key, user.id); },
        std::move(onDone), std::move(onError));
}

void Runner::PostComment(const std::string& connectionName, const std::string& key, const std::string& body, std::function<void()> onDone,
                         std::function<void(std::string)> onError) {
    Act(
        connectionName, key, Capability::Comment, "comment",
        [&key, &body](const Provider& provider, const Connection& connection) { return provider.CommentArgv(connection, key, body); },
        std::move(onDone), std::move(onError));
}

void Runner::PostWorklog(const std::string& connectionName, const std::string& key, const Worklog& worklog, std::function<void()> onDone,
                         std::function<void(std::string)> onError) {
    Act(
        connectionName, key, Capability::Worklog, "worklog",
        [&key, &worklog](const Provider& provider, const Connection& connection) { return provider.WorklogArgv(connection, key, worklog); },
        std::move(onDone), std::move(onError));
}

void Runner::RequestProjectKeys(const std::string& connectionName, std::function<void(std::vector<std::string>)> onComplete,
                                std::function<void(std::string)> onError) {
    Run(
        "project-keys:" + connectionName, ConnectionSubject(connectionName), "listing project keys", connectionName, Capability::ProjectKeys,
        [](const Provider& provider, const Connection& connection) { return provider.ProjectKeysArgv(connection); },
        [connectionName, onComplete = std::move(onComplete), onError](const Provider& provider, const std::string& output,
                                                                      std::optional<int> exitCode) {
            auto result = FinishProjectKeys(provider, connectionName, output, exitCode);
            if (result) {
                onComplete(std::move(*result));
            }
            else {
                onError(std::move(result.error()));
            }
        },
        onError);
}

void Runner::RequestMine(const std::string& connectionName, std::function<void(std::vector<Issue>)> onComplete,
                         std::function<void(std::string)> onError) {
    const std::string subject = ConnectionSubject(connectionName);
    Run(
        "mine:" + connectionName, subject, "listing your issues", connectionName, Capability::Mine,
        [](const Provider& provider, const Connection& connection) { return provider.ListArgv(connection, provider.MineQuery(connection)); },
        [subject, onComplete = std::move(onComplete), onError](const Provider& provider, const std::string& output,
                                                               std::optional<int> exitCode) {
            if (!exitCode || *exitCode != 0) {
                onError(FailureDetail(subject, "listing your issues", exitCode, output));
                return;
            }
            try {
                onComplete(provider.ParseList(output));
            }
            catch (const std::exception& e) {
                onError(subject + ": " + e.what());
            }
        },
        onError);
}

} // namespace ned::editor::tracker
