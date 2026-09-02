#include "DapManager.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "DapConfig.h"

namespace ned::editor::dap {

DapManager::DapManager(ned::ui::EventLoop& eventLoop) : eventLoop_(eventLoop) {
}

DapManager::SessionState DapManager::State() const {
    return state_;
}

std::string DapManager::NormalizePathKey(const std::filesystem::path& path) {
    std::error_code ec;
    // weakly_canonical (same choice HandleRenameFileKey already made): a
    // breakpoint can be toggled in a buffer whose file the debugger will
    // later report via a symlink-free absolute path — both spellings must
    // land on one entry. Falls back to absolute() if resolution fails.
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonical.string();
    }
    return std::filesystem::absolute(path).string();
}

bool DapManager::ToggleBreakpoint(const std::filesystem::path& path, std::size_t line) {
    const std::string          key         = NormalizePathKey(path);
    std::vector<Breakpoint>&   breakpoints = breakpoints_[key];
    const auto it = std::find_if(breakpoints.begin(), breakpoints.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    bool        nowSet;
    if (it != breakpoints.end()) {
        breakpoints.erase(it);
        nowSet = false;
        if (breakpoints.empty()) {
            // Keep the map free of empty entries so BreakpointsForFile and
            // HandleInitializedEvent's per-file loop never see ghosts —
            // but push the now-empty list to a live adapter FIRST, or the
            // removal would never reach it.
            if (client_ && state_ != SessionState::Inactive) {
                SendBreakpointsForFile(key);
            }
            breakpoints_.erase(key);
            return false;
        }
    }
    else {
        breakpoints.push_back(Breakpoint{.line = line});
        std::sort(breakpoints.begin(), breakpoints.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        nowSet = true;
    }
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    return nowSet;
}

std::string DapManager::SetBreakpointCondition(const std::filesystem::path& path, std::size_t line, std::string condition) {
    const std::string        key   = NormalizePathKey(path);
    std::vector<Breakpoint>& lines = breakpoints_[key];
    auto it = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (it == lines.end()) {
        lines.push_back(Breakpoint{.line = line});
        std::sort(lines.begin(), lines.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        it = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    }
    it->condition = condition;
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    std::string status = (condition.empty() ? "Condition cleared at " : "Condition set at ") + path.filename().string() + ":" +
                          std::to_string(line);
    if (!condition.empty() && client_ && state_ != SessionState::Inactive && !capabilities_.conditionalBreakpoints) {
        status += " (adapter did not advertise conditional-breakpoint support -- may be ignored)";
    }
    return status;
}

std::string DapManager::SetBreakpointLogMessage(const std::filesystem::path& path, std::size_t line, std::string logMessage) {
    const std::string        key   = NormalizePathKey(path);
    std::vector<Breakpoint>& lines = breakpoints_[key];
    auto it = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (it == lines.end()) {
        lines.push_back(Breakpoint{.line = line});
        std::sort(lines.begin(), lines.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        it = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    }
    it->logMessage = logMessage;
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    std::string status = (logMessage.empty() ? "Log message cleared at " : "Log message set at ") + path.filename().string() + ":" +
                          std::to_string(line);
    if (!logMessage.empty() && client_ && state_ != SessionState::Inactive && !capabilities_.logPoints) {
        status += " (adapter did not advertise logpoint support -- may be ignored)";
    }
    return status;
}

std::vector<std::size_t> DapManager::BreakpointsForFile(const std::filesystem::path& path) const {
    const auto                it = breakpoints_.find(NormalizePathKey(path));
    std::vector<std::size_t>  lines;
    if (it != breakpoints_.end()) {
        for (const Breakpoint& bp : it->second) {
            lines.push_back(bp.line);
        }
    }
    return lines;
}

std::map<std::string, std::vector<std::size_t>> DapManager::AllBreakpoints() const {
    std::map<std::string, std::vector<std::size_t>> lineOnly;
    for (const auto& [key, breakpoints] : breakpoints_) {
        std::vector<std::size_t>& lines = lineOnly[key];
        for (const Breakpoint& bp : breakpoints) {
            lines.push_back(bp.line);
        }
    }
    return lineOnly;
}

void DapManager::RestoreBreakpoints(std::map<std::string, std::vector<std::size_t>> breakpoints) {
    // Union of old and new keys first, so a live adapter (the robustness
    // guard case -- see the header) also hears about files whose set just
    // became empty, same reasoning as ToggleBreakpoint's erase path.
    std::vector<std::string> affectedKeys;
    for (const auto& [key, lines] : breakpoints_) {
        affectedKeys.push_back(key);
    }
    for (const auto& [key, lines] : breakpoints) {
        affectedKeys.push_back(key);
    }

    // Sorted, deduplicated, and empty-entry-free -- the exact invariants
    // ToggleBreakpoint maintains, enforced here because these lines came
    // from a session file rather than through it. condition/logMessage
    // aren't part of the on-disk shape (see ROADMAP.md) -- every restored
    // entry starts as a plain breakpoint.
    breakpoints_.clear();
    for (auto& [key, lines] : breakpoints) {
        std::sort(lines.begin(), lines.end());
        lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
        if (lines.empty()) {
            continue;
        }
        std::vector<Breakpoint>& converted = breakpoints_[key];
        for (const std::size_t line : lines) {
            converted.push_back(Breakpoint{.line = line});
        }
    }

    if (client_ && state_ != SessionState::Inactive) {
        std::sort(affectedKeys.begin(), affectedKeys.end());
        affectedKeys.erase(std::unique(affectedKeys.begin(), affectedKeys.end()), affectedKeys.end());
        for (const std::string& key : affectedKeys) {
            SendBreakpointsForFile(key);
        }
    }
}

void DapManager::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    if (client_) {
        client_->ExpireStaleRequests(maxAge);
    }
}

std::string DapManager::StartOrContinue(const std::string& language) {
    if (state_ == SessionState::Stopped) {
        client_->SendRequest("continue", Json{{"threadId", CurrentThreadId()}},
                             [this](bool success, const Json&, const std::string& message) {
                                 if (success) {
                                     MarkResumed();
                                 }
                                 else {
                                     EndSession("continue failed: " + message);
                                 }
                             });
        return "Continuing.";
    }
    if (state_ != SessionState::Inactive) {
        return "Debug session already running.";
    }

    const auto launchConfig = DapLaunchConfig(language);
    if (!launchConfig) {
        return "No launch configuration for " + language + " (ned/set-dap-launch).";
    }

    capabilities_ = Capabilities{}; // fresh adapter, fresh capabilities -- see the header's own doc comment

    if (!client_) {
        const auto argv = DapAdapterCommand(language);
        if (!argv) {
            return "No debug adapter configured for " + language + " (ned/set-dap-adapter).";
        }
        try {
            client_ = std::make_unique<DapClient>(*argv, eventLoop_);
        }
        catch (const std::exception& e) {
            client_.reset();
            return std::string("Failed to start debug adapter: ") + e.what();
        }
    }
    // else: a client injected via SetClientForTesting — run the same
    // handshake against it.

    language_ = language;
    state_    = SessionState::Starting;
    WireClient(*client_);

    client_->SendRequest("initialize",
                         Json{
                             {"clientID", "ned"},
                             {"clientName", "ned"},
                             {"adapterID", language},
                             {"linesStartAt1", true},
                             {"columnsStartAt1", true},
                             {"pathFormat", "path"},
                             {"supportsRunInTerminalRequest", false},
                         },
                         [this](bool success, const Json& body, const std::string& message) {
                             if (!success) {
                                 EndSession("initialize failed: " + message);
                                 return;
                             }
                             if (body.contains("supportsConditionalBreakpoints") && body["supportsConditionalBreakpoints"].is_boolean()) {
                                 capabilities_.conditionalBreakpoints = body["supportsConditionalBreakpoints"].get<bool>();
                             }
                             if (body.contains("supportsLogPoints") && body["supportsLogPoints"].is_boolean()) {
                                 capabilities_.logPoints = body["supportsLogPoints"].get<bool>();
                             }
                             if (body.contains("supportsSetVariable") && body["supportsSetVariable"].is_boolean()) {
                                 capabilities_.setVariable = body["supportsSetVariable"].get<bool>();
                             }
                             SendLaunch();
                         });
    return "Starting debug session (" + language + ")...";
}

void DapManager::SendLaunch() {
    const auto launchConfig = DapLaunchConfig(language_);
    Json       arguments    = Json::object();
    if (launchConfig) {
        try {
            arguments = Json::parse(*launchConfig);
        }
        catch (const std::exception& e) {
            EndSession(std::string("launch configuration is not valid JSON: ") + e.what());
            return;
        }
    }
    client_->SendRequest("launch", std::move(arguments), [this](bool success, const Json&, const std::string& message) {
        if (!success) {
            EndSession("launch failed: " + message);
            return;
        }
        if (state_ == SessionState::Starting) {
            state_ = SessionState::Running;
        }
    });
}

void DapManager::WireClient(DapClient& client) {
    client.SetEventHandler("initialized", [this](const Json&) { HandleInitializedEvent(); });
    client.SetEventHandler("stopped", [this](const Json& body) { HandleStoppedEvent(body); });
    client.SetEventHandler("terminated", [this](const Json&) { EndSession("Debug session terminated."); });
    client.SetEventHandler("exited", [this](const Json& body) {
        const int exitCode = body.value("exitCode", 0);
        EndSession("Debuggee exited (code " + std::to_string(exitCode) + ").");
    });
    client.SetOnDisconnected([this](std::string reason) { EndSession("Debug adapter disconnected: " + std::move(reason)); });
}

void DapManager::HandleInitializedEvent() {
    for (const auto& [pathKey, lines] : breakpoints_) {
        (void)lines;
        SendBreakpointsForFile(pathKey);
    }
    client_->SendRequest("configurationDone", Json::object(), [](bool, const Json&, const std::string&) {
        // Nothing to do either way — a failure here surfaces soon enough
        // through the launch response or a terminated event.
    });
}

void DapManager::SendBreakpointsForFile(const std::string& pathKey) {
    Json breakpointsJson = Json::array();
    if (const auto it = breakpoints_.find(pathKey); it != breakpoints_.end()) {
        for (const Breakpoint& bp : it->second) {
            Json entry = Json{{"line", bp.line}};
            if (!bp.condition.empty()) {
                entry["condition"] = bp.condition;
            }
            if (!bp.logMessage.empty()) {
                entry["logMessage"] = bp.logMessage;
            }
            breakpointsJson.push_back(std::move(entry));
        }
    }
    client_->SendRequest("setBreakpoints",
                         Json{
                             {"source", Json{{"path", pathKey}}},
                             {"breakpoints", std::move(breakpointsJson)},
                         },
                         [this, pathKey](bool success, const Json& body, const std::string&) {
                             // Adjusted (snapped) positions in the response are still
                             // ignored -- the gutter shows where the user toggled, not
                             // where the adapter moved it to (documented cut, ROADMAP.md).
                             // "verified" IS tracked now, matched back by index (the
                             // response array is the same order as the request, per
                             // spec) -- it dims the gutter glyph rather than being
                             // dropped on the floor.
                             if (!success || !body.contains("breakpoints") || !body["breakpoints"].is_array()) {
                                 return;
                             }
                             const auto it = breakpoints_.find(pathKey);
                             if (it == breakpoints_.end()) {
                                 return; // toggled off again before the response landed
                             }
                             const Json& results = body["breakpoints"];
                             for (std::size_t i = 0; i < it->second.size() && i < results.size(); ++i) {
                                 if (results[i].contains("verified") && results[i]["verified"].is_boolean()) {
                                     it->second[i].verified = results[i]["verified"].get<bool>();
                                 }
                             }
                         });
}

void DapManager::HandleStoppedEvent(const Json& body) {
    state_                   = SessionState::Stopped;
    stoppedThreadId_         = body.value("threadId", 1);
    focusedThreadId_.reset(); // re-seeded from stoppedThreadId_ via CurrentThreadId() until SelectThread overrides it
    const std::string reason = body.value("reason", "stopped");

    client_->SendRequest("stackTrace",
                         Json{
                             {"threadId", stoppedThreadId_},
                             {"startFrame", 0},
                             {"levels", 1},
                         },
                         [this, reason](bool success, const Json& responseBody, const std::string&) {
                             StoppedInfo info;
                             info.reason = reason;
                             if (success && responseBody.contains("stackFrames") && responseBody["stackFrames"].is_array() &&
                                 !responseBody["stackFrames"].empty()) {
                                 const Json& frame = responseBody["stackFrames"][0];
                                 if (frame.contains("id") && frame["id"].is_number_integer()) {
                                     stoppedFrameId_ = frame["id"].get<int>(); // what Evaluate scopes to
                                 }
                                 if (frame.contains("source") && frame["source"].contains("path") &&
                                     frame["source"]["path"].is_string() && frame.contains("line") &&
                                     frame["line"].is_number_integer()) {
                                     info.path = std::filesystem::path(frame["source"]["path"].get<std::string>());
                                     info.line = static_cast<std::size_t>(std::max(frame["line"].get<int>(), 1));
                                     // Normalized once here, not per frame in Paint() -- see
                                     // CurrentStopKeyAndLine's own doc comment.
                                     currentStop_ = std::make_pair(NormalizePathKey(*info.path), info.line);
                                 }
                             }
                             if (onStopped_) {
                                 onStopped_(info);
                             }
                         });
}

std::string DapManager::Pause() {
    if (state_ == SessionState::Inactive || state_ == SessionState::Starting) {
        return "No debug session.";
    }
    if (state_ == SessionState::Stopped) {
        return "Already stopped.";
    }
    client_->SendRequest("pause", Json{{"threadId", CurrentThreadId() > 0 ? CurrentThreadId() : 1}},
                         [](bool, const Json&, const std::string&) {
                             // The actual stop arrives as a `stopped` event.
                         });
    return "Pause requested.";
}

std::string DapManager::StopSession() {
    if (state_ == SessionState::Inactive) {
        return "No debug session.";
    }
    // Best-effort polite disconnect; teardown below must not depend on the
    // adapter answering (or even still being alive to write to).
    // async-write-queue follow-up: PrepareForGracefulShutdown must be called
    // before this SendRequest -- without it, EndSession destroying client_
    // right below could race the write thread's own stop and silently drop
    // this "disconnect" frame before it ever reaches the wire (see
    // DapClient.h's own header comment).
    try {
        client_->PrepareForGracefulShutdown();
        client_->SendRequest("disconnect", Json{{"terminateDebuggee", true}}, [](bool, const Json&, const std::string&) {});
    }
    catch (const std::exception&) {
        // EPIPE from an already-dead adapter — teardown proceeds regardless.
    }
    EndSession("Debug session stopped.");
    return "Debug session stopped.";
}

void DapManager::MarkResumed() {
    state_ = SessionState::Running;
    currentStop_.reset();
    stoppedFrameId_.reset();
    focusedThreadId_.reset();
}

std::string DapManager::SendStep(const std::string& command, const std::string& label) {
    if (state_ != SessionState::Stopped) {
        return "Not stopped (nothing to step).";
    }
    client_->SendRequest(command, Json{{"threadId", CurrentThreadId()}},
                         [this, command](bool success, const Json&, const std::string& message) {
                             if (success) {
                                 MarkResumed(); // the landing spot arrives as the next `stopped` event
                             }
                             else {
                                 EndSession(command + " failed: " + message);
                             }
                         });
    return label + "...";
}

std::string DapManager::StepOver() {
    return SendStep("next", "Stepping over");
}

std::string DapManager::StepInto() {
    return SendStep("stepIn", "Stepping into");
}

std::string DapManager::StepOut() {
    return SendStep("stepOut", "Stepping out");
}

std::optional<std::pair<std::string, std::size_t>> DapManager::CurrentStopKeyAndLine() const {
    return currentStop_;
}

std::vector<std::size_t> DapManager::BreakpointLinesForKey(const std::string& key) const {
    std::vector<std::size_t> lines;
    const auto                it = breakpoints_.find(key);
    if (it != breakpoints_.end()) {
        for (const Breakpoint& bp : it->second) {
            lines.push_back(bp.line);
        }
    }
    return lines;
}

std::vector<DapManager::Breakpoint> DapManager::BreakpointsForKey(const std::string& key) const {
    const auto it = breakpoints_.find(key);
    return it != breakpoints_.end() ? it->second : std::vector<Breakpoint>{};
}

int DapManager::CurrentThreadId() const {
    return focusedThreadId_.value_or(stoppedThreadId_);
}

void DapManager::RequestStackTrace(std::function<void(std::vector<StackFrame>)> callback) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback({});
        return;
    }
    client_->SendRequest("stackTrace", Json{{"threadId", CurrentThreadId()}, {"startFrame", 0}, {"levels", 20}},
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<StackFrame> frames;
                             if (success && body.contains("stackFrames") && body["stackFrames"].is_array()) {
                                 for (const Json& frameJson : body["stackFrames"]) {
                                     StackFrame frame;
                                     frame.id   = frameJson.value("id", 0);
                                     frame.name = frameJson.value("name", "");
                                     if (frameJson.contains("source") && frameJson["source"].contains("path") &&
                                         frameJson["source"]["path"].is_string() && frameJson.contains("line") &&
                                         frameJson["line"].is_number_integer()) {
                                         frame.path = std::filesystem::path(frameJson["source"]["path"].get<std::string>());
                                         frame.line = static_cast<std::size_t>(std::max(frameJson["line"].get<int>(), 1));
                                     }
                                     frames.push_back(std::move(frame));
                                 }
                             }
                             callback(std::move(frames));
                         });
}

void DapManager::RequestScopes(int frameId, std::function<void(std::vector<Scope>)> callback) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback({});
        return;
    }
    client_->SendRequest("scopes", Json{{"frameId", frameId}},
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<Scope> scopes;
                             if (success && body.contains("scopes") && body["scopes"].is_array()) {
                                 for (const Json& scopeJson : body["scopes"]) {
                                     scopes.push_back(Scope{
                                         .name               = scopeJson.value("name", ""),
                                         .variablesReference = scopeJson.value("variablesReference", 0),
                                     });
                                 }
                             }
                             callback(std::move(scopes));
                         });
}

void DapManager::RequestVariables(int variablesReference, std::function<void(std::vector<Variable>)> callback) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback({});
        return;
    }
    client_->SendRequest("variables", Json{{"variablesReference", variablesReference}},
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<Variable> variables;
                             if (success && body.contains("variables") && body["variables"].is_array()) {
                                 for (const Json& variableJson : body["variables"]) {
                                     variables.push_back(Variable{
                                         .name               = variableJson.value("name", ""),
                                         .value              = variableJson.value("value", ""),
                                         .type               = variableJson.value("type", ""),
                                         .variablesReference = variableJson.value("variablesReference", 0),
                                     });
                                 }
                             }
                             callback(std::move(variables));
                         });
}

void DapManager::Evaluate(const std::string& expression, std::function<void(bool, std::string)> callback, std::string context) {
    if (!client_ || state_ == SessionState::Inactive || state_ == SessionState::Starting) {
        callback(false, "No debug session.");
        return;
    }
    Json arguments = {{"expression", expression}, {"context", std::move(context)}};
    if (stoppedFrameId_) {
        arguments["frameId"] = *stoppedFrameId_;
    }
    client_->SendRequest("evaluate", std::move(arguments),
                         [callback = std::move(callback)](bool success, const Json& body, const std::string& message) {
                             if (success) {
                                 callback(true, body.value("result", ""));
                             }
                             else {
                                 callback(false, message);
                             }
                         });
}

void DapManager::AddWatch(std::string expression) {
    watches_.push_back(std::move(expression));
}

void DapManager::RemoveWatchAt(std::size_t index) {
    if (index < watches_.size()) {
        watches_.erase(watches_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

const std::vector<std::string>& DapManager::Watches() const {
    return watches_;
}

void DapManager::RequestThreads(std::function<void(std::vector<Thread>)> callback) {
    if (!client_ || state_ == SessionState::Inactive || state_ == SessionState::Starting) {
        callback({});
        return;
    }
    client_->SendRequest("threads", Json::object(),
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<Thread> threads;
                             if (success && body.contains("threads") && body["threads"].is_array()) {
                                 for (const Json& threadJson : body["threads"]) {
                                     threads.push_back(Thread{
                                         .id   = threadJson.value("id", 0),
                                         .name = threadJson.value("name", ""),
                                     });
                                 }
                             }
                             callback(std::move(threads));
                         });
}

void DapManager::SelectThread(int threadId, std::function<void(bool)> callback) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback(false);
        return;
    }
    focusedThreadId_ = threadId;
    client_->SendRequest("stackTrace", Json{{"threadId", threadId}, {"startFrame", 0}, {"levels", 1}},
                         [this, callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             if (success && body.contains("stackFrames") && body["stackFrames"].is_array() &&
                                 !body["stackFrames"].empty() && body["stackFrames"][0].contains("id") &&
                                 body["stackFrames"][0]["id"].is_number_integer()) {
                                 stoppedFrameId_ = body["stackFrames"][0]["id"].get<int>();
                             }
                             callback(success);
                         });
}

void DapManager::SetVariable(int variablesReference, const std::string& name, const std::string& value,
                              std::function<void(SetVariableResult)> callback) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback(SetVariableResult{.success = false, .errorMessage = "No debug session."});
        return;
    }
    client_->SendRequest("setVariable", Json{{"variablesReference", variablesReference}, {"name", name}, {"value", value}},
                         [callback = std::move(callback)](bool success, const Json& body, const std::string& message) {
                             SetVariableResult result;
                             result.success = success;
                             if (success) {
                                 result.value              = body.value("value", "");
                                 result.type                = body.value("type", "");
                                 result.variablesReference = body.value("variablesReference", 0);
                             }
                             else {
                                 result.errorMessage = message;
                             }
                             callback(std::move(result));
                         });
}

void DapManager::EndSession(std::string reason) {
    if (state_ == SessionState::Inactive) {
        return; // e.g. disconnect EOF arriving after an explicit StopSession already tore down
    }
    state_           = SessionState::Inactive;
    stoppedThreadId_ = 0;
    currentStop_.reset();
    stoppedFrameId_.reset();
    focusedThreadId_.reset();
    capabilities_ = Capabilities{};
    // lsp-use-after-free follow-up: client_ used to move into retired_ here
    // instead of destroying in place, deferring to the next StartOrContinue
    // ("safe here: nothing of a previous session is on the stack" -- true,
    // but confirmed live elsewhere in this codebase that deferring isn't
    // actually what makes this safe: LspClient's own identical pattern still
    // raced a periodic tick against a background thread's own Post()ed
    // callback for the same object). The real fix now lives in DapClient
    // itself (alive_, see LspClient.h's header comment) -- a stray Post()ed
    // callback safely no-ops instead of touching freed memory regardless of
    // when this destroys the object, so plain immediate destruction is safe.
    client_.reset();
    if (onSessionEnded_) {
        onSessionEnded_(std::move(reason));
    }
}

void DapManager::SetOnStopped(std::function<void(const StoppedInfo&)> handler) {
    onStopped_ = std::move(handler);
}

void DapManager::SetOnSessionEnded(std::function<void(std::string)> handler) {
    onSessionEnded_ = std::move(handler);
}

DapClient& DapManager::SetClientForTesting(std::unique_ptr<DapClient> client) {
    client_ = std::move(client);
    return *client_;
}

} // namespace ned::editor::dap
