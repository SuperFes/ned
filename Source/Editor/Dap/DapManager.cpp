#include "DapManager.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "DapConfig.h"

namespace ned::editor::dap {

namespace {

    // DAP round 5: readMemory's response `data` field is base64. No shared
    // base64 helper exists in this codebase -- Clipboard.cpp keeps its own
    // file-local Base64Encode, not exported -- so this is this file's own
    // decoder, same "each consumer keeps its own" precedent.
    std::vector<std::uint8_t> Base64Decode(std::string_view text) {
        auto valueOf = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') {
                return c - 'A';
            }
            if (c >= 'a' && c <= 'z') {
                return c - 'a' + 26;
            }
            if (c >= '0' && c <= '9') {
                return c - '0' + 52;
            }
            if (c == '+') {
                return 62;
            }
            if (c == '/') {
                return 63;
            }
            return -1; // padding ('=') or whitespace/garbage -- both just stop that group short
        };

        std::vector<std::uint8_t> result;
        result.reserve((text.size() / 4) * 3);

        int buffer       = 0;
        int bitsInBuffer = 0;
        for (const char c : text) {
            const int value = valueOf(c);
            if (value < 0) {
                continue;
            }
            buffer = (buffer << 6) | value;
            bitsInBuffer += 6;
            if (bitsInBuffer >= 8) {
                bitsInBuffer -= 8;
                result.push_back(static_cast<std::uint8_t>((buffer >> bitsInBuffer) & 0xFF));
            }
        }
        return result;
    }

} // namespace

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

std::string DapManager::SetBreakpointHitCondition(const std::filesystem::path& path, std::size_t line, std::string hitCondition) {
    const std::string        key   = NormalizePathKey(path);
    std::vector<Breakpoint>& lines = breakpoints_[key];
    auto                     it    = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (it == lines.end()) {
        lines.push_back(Breakpoint{.line = line});
        std::sort(lines.begin(), lines.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        it = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    }
    it->hitCondition = hitCondition;
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    std::string status = (hitCondition.empty() ? "Hit condition cleared at " : "Hit condition set at ") +
                         path.filename().string() + ":" + std::to_string(line);
    if (!hitCondition.empty() && client_ && state_ != SessionState::Inactive && !capabilities_.hitConditionalBreakpoints) {
        status += " (adapter did not advertise hit-conditional-breakpoint support -- may be ignored)";
    }
    return status;
}

bool DapManager::ToggleFunctionBreakpoint(std::string name) {
    const auto it = std::find(functionBreakpoints_.begin(), functionBreakpoints_.end(), name);
    bool       nowSet;
    if (it != functionBreakpoints_.end()) {
        functionBreakpoints_.erase(it);
        nowSet = false;
    }
    else {
        functionBreakpoints_.push_back(std::move(name));
        std::sort(functionBreakpoints_.begin(), functionBreakpoints_.end());
        nowSet = true;
    }
    if (client_ && state_ != SessionState::Inactive) {
        SendFunctionBreakpoints();
    }
    return nowSet;
}

const std::vector<std::string>& DapManager::FunctionBreakpoints() const {
    return functionBreakpoints_;
}

const std::vector<DapManager::ExceptionFilter>& DapManager::AvailableExceptionFilters() const {
    return exceptionFilters_;
}

const std::set<std::string>& DapManager::EnabledExceptionFilters() const {
    return enabledExceptionFilters_;
}

void DapManager::SetExceptionBreakpointFilters(std::set<std::string> ids) {
    enabledExceptionFilters_ = std::move(ids);
    if (client_ && state_ != SessionState::Inactive) {
        SendExceptionBreakpoints();
    }
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

std::map<std::string, std::vector<DapManager::PersistedBreakpoint>> DapManager::AllBreakpoints() const {
    std::map<std::string, std::vector<PersistedBreakpoint>> persisted;
    for (const auto& [key, breakpoints] : breakpoints_) {
        std::vector<PersistedBreakpoint>& entries = persisted[key];
        for (const Breakpoint& bp : breakpoints) {
            entries.push_back(PersistedBreakpoint{
                .line         = bp.line,
                .condition    = bp.condition,
                .logMessage   = bp.logMessage,
                .hitCondition = bp.hitCondition,
            });
        }
    }
    return persisted;
}

void DapManager::RestoreBreakpoints(std::map<std::string, std::vector<PersistedBreakpoint>> breakpoints) {
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

    // Sorted-by-line, deduplicated-by-line (a duplicate line collapses to
    // its first occurrence, discarding whichever condition/logMessage/
    // hitCondition it carried -- session-file data is trusted to already be
    // well-formed, same as the pre-round-2 line-only shape was), and
    // empty-entry-free -- the exact invariants ToggleBreakpoint maintains.
    // verified/actualLine are NOT restored (see PersistedBreakpoint) --
    // every entry starts exactly like a freshly-toggled breakpoint.
    breakpoints_.clear();
    for (auto& [key, entries] : breakpoints) {
        std::sort(entries.begin(), entries.end(),
                  [](const PersistedBreakpoint& a, const PersistedBreakpoint& b) { return a.line < b.line; });
        entries.erase(std::unique(entries.begin(), entries.end(),
                                  [](const PersistedBreakpoint& a, const PersistedBreakpoint& b) { return a.line == b.line; }),
                      entries.end());
        if (entries.empty()) {
            continue;
        }
        std::vector<Breakpoint>& converted = breakpoints_[key];
        for (const PersistedBreakpoint& entry : entries) {
            converted.push_back(Breakpoint{
                .line         = entry.line,
                .condition    = entry.condition,
                .logMessage   = entry.logMessage,
                .hitCondition = entry.hitCondition,
            });
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
    if (!DapLaunchConfig(language)) {
        return "No launch configuration for " + language + " (ned/set-dap-launch).";
    }
    return BeginSession(language, /*attach=*/false);
}

std::string DapManager::Attach(const std::string& language) {
    if (state_ != SessionState::Inactive) {
        return "Debug session already running.";
    }
    if (!DapAttachConfig(language)) {
        return "No attach configuration for " + language + " (ned/set-dap-attach).";
    }
    return BeginSession(language, /*attach=*/true);
}

std::string DapManager::BeginSession(const std::string& language, bool attach) {
    capabilities_ = Capabilities{}; // fresh adapter, fresh capabilities -- see the header's own doc comment
    exceptionFilters_.clear();
    enabledExceptionFilters_.clear();
    isAttach_ = attach;

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
                             if (body.contains("supportsHitConditionalBreakpoints") &&
                                 body["supportsHitConditionalBreakpoints"].is_boolean()) {
                                 capabilities_.hitConditionalBreakpoints = body["supportsHitConditionalBreakpoints"].get<bool>();
                             }
                             if (body.contains("supportsFunctionBreakpoints") && body["supportsFunctionBreakpoints"].is_boolean()) {
                                 capabilities_.functionBreakpoints = body["supportsFunctionBreakpoints"].get<bool>();
                             }
                             if (body.contains("supportsRestartFrame") && body["supportsRestartFrame"].is_boolean()) {
                                 capabilities_.restartFrame = body["supportsRestartFrame"].get<bool>();
                             }
                             if (body.contains("supportsDisassembleRequest") && body["supportsDisassembleRequest"].is_boolean()) {
                                 capabilities_.disassemble = body["supportsDisassembleRequest"].get<bool>();
                             }
                             if (body.contains("supportsReadMemoryRequest") && body["supportsReadMemoryRequest"].is_boolean()) {
                                 capabilities_.readMemory = body["supportsReadMemoryRequest"].get<bool>();
                             }
                             if (body.contains("exceptionBreakpointFilters") && body["exceptionBreakpointFilters"].is_array()) {
                                 for (const Json& filterJson : body["exceptionBreakpointFilters"]) {
                                     ExceptionFilter filter;
                                     filter.id             = filterJson.value("filter", "");
                                     filter.label          = filterJson.value("label", filter.id);
                                     filter.defaultEnabled = filterJson.value("default", false);
                                     if (filter.id.empty()) {
                                         continue;
                                     }
                                     if (filter.defaultEnabled) {
                                         enabledExceptionFilters_.insert(filter.id);
                                     }
                                     exceptionFilters_.push_back(std::move(filter));
                                 }
                             }
                             SendLaunchOrAttach();
                         });
    return std::string("Starting debug session (") + language + ")...";
}

void DapManager::SendLaunchOrAttach() {
    const auto config    = isAttach_ ? DapAttachConfig(language_) : DapLaunchConfig(language_);
    Json       arguments = Json::object();
    if (config) {
        try {
            arguments = Json::parse(*config);
        }
        catch (const std::exception& e) {
            EndSession(std::string(isAttach_ ? "attach" : "launch") + " configuration is not valid JSON: " + e.what());
            return;
        }
    }
    const std::string command = isAttach_ ? "attach" : "launch";
    client_->SendRequest(command, std::move(arguments), [this, command](bool success, const Json&, const std::string& message) {
        if (!success) {
            EndSession(command + " failed: " + message);
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
    // DAP round 3: function/exception breakpoints go out here too, same as
    // the per-file setBreakpoints loop above -- everything before
    // configurationDone.
    SendFunctionBreakpoints();
    SendExceptionBreakpoints();
    client_->SendRequest("configurationDone", Json::object(), [](bool, const Json&, const std::string&) {
        // Nothing to do either way — a failure here surfaces soon enough
        // through the launch response or a terminated event.
    });
}

void DapManager::SendFunctionBreakpoints() {
    Json breakpointsJson = Json::array();
    for (const std::string& name : functionBreakpoints_) {
        breakpointsJson.push_back(Json{{"name", name}});
    }
    client_->SendRequest("setFunctionBreakpoints", Json{{"breakpoints", std::move(breakpointsJson)}},
                         [](bool, const Json&, const std::string&) {
                             // No per-entry "verified" tracking for function
                             // breakpoints yet (documented cut, ROADMAP.md) --
                             // same "fire and let a failure surface via the
                             // session ending" shape as configurationDone above.
                         });
}

void DapManager::SendExceptionBreakpoints() {
    Json filtersJson = Json::array();
    for (const std::string& id : enabledExceptionFilters_) {
        filtersJson.push_back(id);
    }
    client_->SendRequest("setExceptionBreakpoints", Json{{"filters", std::move(filtersJson)}},
                         [](bool, const Json&, const std::string&) {
                             // Same fire-and-forget shape as setFunctionBreakpoints above.
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
            if (!bp.hitCondition.empty()) {
                entry["hitCondition"] = bp.hitCondition;
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
                             // "verified" IS tracked now, matched back by index (the
                             // response array is the same order as the request, per
                             // spec) -- it dims the gutter glyph rather than being
                             // dropped on the floor. DAP round 4: the adapter's own
                             // snapped "line" per entry is tracked too (actualLine) --
                             // the gutter shows it in place of the requested line when
                             // it differs; editing operations still address the
                             // requested line (see Breakpoint::actualLine's own doc
                             // comment).
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
                                 if (results[i].contains("line") && results[i]["line"].is_number_integer()) {
                                     it->second[i].actualLine = static_cast<std::size_t>(std::max(results[i]["line"].get<int>(), 1));
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
        // DAP round 3: an attached session never kills a process ned didn't
        // start -- only a launched one does.
        client_->SendRequest("disconnect", Json{{"terminateDebuggee", !isAttach_}}, [](bool, const Json&, const std::string&) {});
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

std::string DapManager::RestartFrame(int frameId) {
    if (state_ != SessionState::Stopped) {
        return "Not stopped (nothing to restart).";
    }
    client_->SendRequest("restartFrame", Json{{"frameId", frameId}},
                         [this](bool success, const Json&, const std::string& message) {
                             if (success) {
                                 MarkResumed(); // the landing spot arrives as the next `stopped` event
                             }
                             else {
                                 EndSession("restartFrame failed: " + message);
                             }
                         });
    std::string status = "Restarting frame...";
    if (!capabilities_.restartFrame) {
        status += " (adapter did not advertise restart-frame support -- may be ignored)";
    }
    return status;
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
                                     if (frameJson.contains("instructionPointerReference") &&
                                         frameJson["instructionPointerReference"].is_string()) {
                                         frame.instructionPointerReference = frameJson["instructionPointerReference"].get<std::string>();
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
                                         .memoryReference    = variableJson.value("memoryReference", ""),
                                     });
                                 }
                             }
                             callback(std::move(variables));
                         });
}

void DapManager::RequestDisassembly(const std::string& memoryReference, long instructionOffset, int instructionCount,
                                    std::function<void(std::vector<DisassembledInstruction>)> callback) {
    if (!client_ || state_ != SessionState::Stopped || memoryReference.empty()) {
        callback({});
        return;
    }
    client_->SendRequest("disassemble",
                         Json{
                             {"memoryReference", memoryReference},
                             {"offset", 0},
                             {"instructionOffset", instructionOffset},
                             {"instructionCount", instructionCount},
                             {"resolveSymbols", true},
                         },
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<DisassembledInstruction> instructions;
                             if (success && body.contains("instructions") && body["instructions"].is_array()) {
                                 for (const Json& instructionJson : body["instructions"]) {
                                     DisassembledInstruction instruction;
                                     instruction.address          = instructionJson.value("address", "");
                                     instruction.instructionBytes = instructionJson.value("instructionBytes", "");
                                     instruction.instruction      = instructionJson.value("instruction", "");
                                     if (instructionJson.contains("location") && instructionJson["location"].contains("path") &&
                                         instructionJson["location"]["path"].is_string() && instructionJson.contains("line") &&
                                         instructionJson["line"].is_number_integer()) {
                                         instruction.path = std::filesystem::path(instructionJson["location"]["path"].get<std::string>());
                                         instruction.line = static_cast<std::size_t>(std::max(instructionJson["line"].get<int>(), 1));
                                     }
                                     instructions.push_back(std::move(instruction));
                                 }
                             }
                             callback(std::move(instructions));
                         });
}

void DapManager::RequestMemory(const std::string& memoryReference, long offset, std::size_t count,
                               std::function<void(bool success, MemoryBlock)> callback) {
    if (!client_ || state_ != SessionState::Stopped || memoryReference.empty()) {
        callback(false, MemoryBlock{});
        return;
    }
    client_->SendRequest("readMemory", Json{{"memoryReference", memoryReference}, {"offset", offset}, {"count", count}},
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             if (!success) {
                                 callback(false, MemoryBlock{});
                                 return;
                             }
                             // A fully-unreadable range is still a successful
                             // response, just with "data" absent and
                             // unreadableBytes covering the whole request --
                             // the DAP spec's own shape, not an error.
                             MemoryBlock block;
                             block.address = body.value("address", "");
                             if (body.contains("data") && body["data"].is_string()) {
                                 block.data = Base64Decode(body["data"].get<std::string>());
                             }
                             block.unreadableBytes = body.value("unreadableBytes", static_cast<std::size_t>(0));
                             callback(true, std::move(block));
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

void DapManager::RestoreWatches(std::vector<std::string> watches) {
    watches_ = std::move(watches);
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
    // DAP round 3: exception filters/isAttach_ are per-session, same reset
    // policy as capabilities_ above -- the next BeginSession re-seeds them
    // regardless, this just keeps a post-EndSession query honest.
    exceptionFilters_.clear();
    enabledExceptionFilters_.clear();
    isAttach_ = false;
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
