#include "Manager.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "Config.h"
#include "Editor/Sparkline.h"

namespace ned::editor::dap {

namespace {

    // dap-anchored-breakpoints follow-up: a 1-based DAP line as a byte offset in the
    // buffer, clamped -- a breakpoint restored from a session file can name a line the
    // file no longer has.
    std::size_t LineStartOffset(const text::Buffer& buffer, std::size_t dapLine) {
        const std::size_t index = dapLine > 0 ? dapLine - 1 : 0;
        return buffer.Content().LineToByteOffset(std::min(index, buffer.Content().LineCount() - 1));
    }

    // Debugging wishlist: watch-history sparkline -- caps how many recent
    // stops' values are kept per watch, matching Editor/Sparkline.h's own
    // default maxWidth so a full history renders one glyph per point with
    // no downsampling in the common case.
    constexpr std::size_t kMaxWatchHistoryPoints = 40;

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

Manager::Manager(ned::ui::EventLoop& eventLoop) : eventLoop_(eventLoop) {
}

Manager::SessionState Manager::State() const {
    return state_;
}

std::string Manager::NormalizePathKey(const std::filesystem::path& path) {
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

bool Manager::ToggleBreakpoint(const std::filesystem::path& path, std::size_t line) {
    const std::string        key         = NormalizePathKey(path);
    std::vector<Breakpoint>& breakpoints = breakpoints_[key];
    const auto               it          = std::find_if(breakpoints.begin(), breakpoints.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    bool                     nowSet;
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
            NotifyBreakpointsChanged();
            return false;
        }
    }
    else {
        breakpoints.push_back(Breakpoint{.line = line, .id = nextBreakpointId_++});
        std::sort(breakpoints.begin(), breakpoints.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        nowSet = true;
    }
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    NotifyBreakpointsChanged();
    if (nowSet) {
        // Asked only for a breakpoint that was just SET, and only of an
        // adapter that offered to answer -- see SnapBreakpointToValidLine.
        SnapBreakpointToValidLine(key, line);
    }
    return nowSet;
}

bool Manager::SetBreakpointEnabled(const std::filesystem::path& path, std::size_t line, bool enabled) {
    const std::string key  = NormalizePathKey(path);
    const auto        file = breakpoints_.find(key);
    if (file == breakpoints_.end()) {
        return false;
    }
    const auto it = std::find_if(file->second.begin(), file->second.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (it == file->second.end() || it->enabled == enabled) {
        return it != file->second.end();
    }
    it->enabled = enabled;
    // A disabled breakpoint keeps whatever the adapter last said about it,
    // which would be a stale "verified" claim about a breakpoint that is no
    // longer set. Reset to the same optimistic default a fresh toggle gets.
    if (!enabled) {
        it->verified   = true;
        it->actualLine = 0;
    }
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    NotifyBreakpointsChanged();
    return true;
}

bool Manager::RemoveBreakpoint(const std::filesystem::path& path, std::size_t line) {
    const std::string key  = NormalizePathKey(path);
    const auto        file = breakpoints_.find(key);
    if (file == breakpoints_.end()) {
        return false;
    }
    const auto it = std::find_if(file->second.begin(), file->second.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (it == file->second.end()) {
        return false;
    }
    file->second.erase(it);
    // Same "push the now-empty list before erasing the map entry" ordering
    // ToggleBreakpoint's own removal path documents.
    const bool nowEmpty = file->second.empty();
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    if (nowEmpty) {
        breakpoints_.erase(key);
    }
    NotifyBreakpointsChanged();
    return true;
}

void Manager::ClearSourceBreakpoints() {
    if (breakpoints_.empty()) {
        return;
    }
    std::vector<std::string> keys;
    keys.reserve(breakpoints_.size());
    for (const auto& [key, entries] : breakpoints_) {
        keys.push_back(key);
    }
    for (const std::string& key : keys) {
        breakpoints_[key].clear();
        if (client_ && state_ != SessionState::Inactive) {
            SendBreakpointsForFile(key); // the empty list is what actually clears the adapter
        }
    }
    breakpoints_.clear();
    NotifyBreakpointsChanged();
}

std::string Manager::SetBreakpointCondition(const std::filesystem::path& path, std::size_t line, std::string condition) {
    const std::string        key   = NormalizePathKey(path);
    std::vector<Breakpoint>& lines = breakpoints_[key];
    auto                     it    = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (it == lines.end()) {
        lines.push_back(Breakpoint{.line = line, .id = nextBreakpointId_++});
        std::sort(lines.begin(), lines.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        it = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    }
    it->condition = condition;
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    NotifyBreakpointsChanged();
    std::string status = (condition.empty() ? "Condition cleared at " : "Condition set at ") + path.filename().string() + ":" +
                         std::to_string(line);
    if (!condition.empty() && client_ && state_ != SessionState::Inactive && !capabilities_.conditionalBreakpoints) {
        status += " (adapter did not advertise conditional-breakpoint support -- may be ignored)";
    }
    return status;
}

std::string Manager::SetBreakpointLogMessage(const std::filesystem::path& path, std::size_t line, std::string logMessage) {
    const std::string        key   = NormalizePathKey(path);
    std::vector<Breakpoint>& lines = breakpoints_[key];
    auto                     it    = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (it == lines.end()) {
        lines.push_back(Breakpoint{.line = line, .id = nextBreakpointId_++});
        std::sort(lines.begin(), lines.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        it = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    }
    it->logMessage = logMessage;
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    NotifyBreakpointsChanged();
    std::string status = (logMessage.empty() ? "Log message cleared at " : "Log message set at ") + path.filename().string() + ":" +
                         std::to_string(line);
    if (!logMessage.empty() && client_ && state_ != SessionState::Inactive && !capabilities_.logPoints) {
        status += " (adapter did not advertise logpoint support -- may be ignored)";
    }
    return status;
}

std::string Manager::SetBreakpointHitCondition(const std::filesystem::path& path, std::size_t line, std::string hitCondition) {
    const std::string        key   = NormalizePathKey(path);
    std::vector<Breakpoint>& lines = breakpoints_[key];
    auto                     it    = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (it == lines.end()) {
        lines.push_back(Breakpoint{.line = line, .id = nextBreakpointId_++});
        std::sort(lines.begin(), lines.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        it = std::find_if(lines.begin(), lines.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    }
    it->hitCondition = hitCondition;
    if (client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
    NotifyBreakpointsChanged();
    std::string status = (hitCondition.empty() ? "Hit condition cleared at " : "Hit condition set at ") +
                         path.filename().string() + ":" + std::to_string(line);
    if (!hitCondition.empty() && client_ && state_ != SessionState::Inactive && !capabilities_.hitConditionalBreakpoints) {
        status += " (adapter did not advertise hit-conditional-breakpoint support -- may be ignored)";
    }
    return status;
}

namespace {

    auto FindFunctionBreakpoint(std::vector<Manager::FunctionBreakpoint>& breakpoints, const std::string& name) {
        return std::find_if(breakpoints.begin(), breakpoints.end(),
                            [&name](const Manager::FunctionBreakpoint& bp) { return bp.name == name; });
    }

} // namespace

bool Manager::ToggleFunctionBreakpoint(std::string name) {
    const auto it = FindFunctionBreakpoint(functionBreakpoints_, name);
    bool       nowSet;
    if (it != functionBreakpoints_.end()) {
        functionBreakpoints_.erase(it);
        nowSet = false;
    }
    else {
        functionBreakpoints_.push_back(FunctionBreakpoint{.name = std::move(name)});
        std::sort(functionBreakpoints_.begin(), functionBreakpoints_.end(),
                  [](const FunctionBreakpoint& a, const FunctionBreakpoint& b) { return a.name < b.name; });
        nowSet = true;
    }
    if (client_ && state_ != SessionState::Inactive) {
        SendFunctionBreakpoints();
    }
    NotifyBreakpointsChanged();
    return nowSet;
}

bool Manager::SetFunctionBreakpointEnabled(const std::string& name, bool enabled) {
    const auto it = FindFunctionBreakpoint(functionBreakpoints_, name);
    if (it == functionBreakpoints_.end()) {
        return false;
    }
    if (it->enabled != enabled) {
        it->enabled = enabled;
        if (client_ && state_ != SessionState::Inactive) {
            SendFunctionBreakpoints();
        }
        NotifyBreakpointsChanged();
    }
    return true;
}

bool Manager::RemoveFunctionBreakpoint(const std::string& name) {
    const auto it = FindFunctionBreakpoint(functionBreakpoints_, name);
    if (it == functionBreakpoints_.end()) {
        return false;
    }
    functionBreakpoints_.erase(it);
    if (client_ && state_ != SessionState::Inactive) {
        SendFunctionBreakpoints();
    }
    NotifyBreakpointsChanged();
    return true;
}

void Manager::RestoreFunctionBreakpoints(std::vector<FunctionBreakpoint> breakpoints) {
    std::sort(breakpoints.begin(), breakpoints.end(),
              [](const FunctionBreakpoint& a, const FunctionBreakpoint& b) { return a.name < b.name; });
    breakpoints.erase(std::unique(breakpoints.begin(), breakpoints.end(),
                                  [](const FunctionBreakpoint& a, const FunctionBreakpoint& b) { return a.name == b.name; }),
                      breakpoints.end());
    functionBreakpoints_ = std::move(breakpoints);
    if (client_ && state_ != SessionState::Inactive) {
        SendFunctionBreakpoints();
    }
    NotifyBreakpointsChanged();
}

void Manager::ClearFunctionBreakpoints() {
    if (functionBreakpoints_.empty()) {
        return;
    }
    functionBreakpoints_.clear();
    if (client_ && state_ != SessionState::Inactive) {
        SendFunctionBreakpoints();
    }
    NotifyBreakpointsChanged();
}

const std::vector<Manager::FunctionBreakpoint>& Manager::FunctionBreakpoints() const {
    return functionBreakpoints_;
}

void Manager::SetOnBreakpointsChanged(std::function<void()> handler) {
    onBreakpointsChanged_ = std::move(handler);
}

void Manager::SetOnStatusMessage(std::function<void(std::string)> handler) {
    onStatusMessage_ = std::move(handler);
}

void Manager::ReportStatus(std::string message) {
    if (onStatusMessage_) {
        onStatusMessage_(std::move(message));
    }
}

void Manager::SnapBreakpointToValidLine(const std::string& pathKey, std::size_t line) {
    if (!client_ || state_ == SessionState::Inactive || state_ == SessionState::Starting ||
        !capabilities_.breakpointLocations) {
        return;
    }
    // A window rather than the whole file: the question is "where does the
    // statement this line belongs to actually start", and an answer dozens
    // of lines away would be a different statement, not a correction.
    constexpr std::size_t kSnapWindow = 8;
    RequestBreakpointLocations(std::filesystem::path(pathKey), line, line + kSnapWindow,
                               [this, pathKey, line](std::vector<std::size_t> valid) {
                                   if (valid.empty() || std::find(valid.begin(), valid.end(), line) != valid.end()) {
                                       return; // no opinion, or the line was already fine
                                   }
                                   const auto file = breakpoints_.find(pathKey);
                                   if (file == breakpoints_.end()) {
                                       return; // removed while the request was in flight
                                   }
                                   const auto it = std::find_if(file->second.begin(), file->second.end(),
                                                                [line](const Breakpoint& bp) { return bp.line == line; });
                                   if (it == file->second.end()) {
                                       return;
                                   }
                                   const std::size_t target = valid.front();
                                   // Already a breakpoint where this one would land: drop the
                                   // new one rather than create a duplicate line, which
                                   // BreakpointsForFile's callers all assume cannot happen.
                                   const bool occupied = std::any_of(file->second.begin(), file->second.end(),
                                                                     [target](const Breakpoint& bp) { return bp.line == target; });
                                   if (occupied) {
                                       file->second.erase(it);
                                       ReportStatus("Line " + std::to_string(line) + " cannot hold a breakpoint; one is already set at " +
                                                    std::to_string(target) + ".");
                                   }
                                   else {
                                       it->line = target;
                                       std::sort(file->second.begin(), file->second.end(),
                                                 [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
                                       ReportStatus("Breakpoint moved to line " + std::to_string(target) +
                                                    " -- line " + std::to_string(line) + " cannot hold one.");
                                   }
                                   if (file->second.empty()) {
                                       breakpoints_.erase(pathKey);
                                   }
                                   SendBreakpointsForFile(pathKey);
                                   NotifyBreakpointsChanged();
                               });
}

void Manager::NotifyBreakpointsChanged() {
    if (onBreakpointsChanged_) {
        onBreakpointsChanged_();
    }
}

void Manager::SetState(SessionState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    if (onSessionStateChanged_) {
        onSessionStateChanged_(state_);
    }
}

void Manager::SetOnSessionStateChanged(std::function<void(SessionState)> handler) {
    onSessionStateChanged_ = std::move(handler);
}

void Manager::SelectFrame(int frameId) {
    stoppedFrameId_ = frameId;
    RefreshFrameLocals();
}

const std::map<std::string, std::string>& Manager::FrameLocals() const {
    return frameLocals_;
}

void Manager::SetFrameLocalsTrackingEnabled(bool enabled) {
    if (frameLocalsTracking_ == enabled) {
        return;
    }
    frameLocalsTracking_ = enabled;
    RefreshFrameLocals(); // fills in if just enabled at a stop, clears if just disabled
}

void Manager::RefreshFrameLocals() {
    // Every outstanding response belongs to the frame that was focused when
    // it was issued; bumping first is what makes a stale one droppable.
    ++frameLocalsGeneration_;
    frameLocals_.clear();
    if (!frameLocalsTracking_ || !client_ || state_ != SessionState::Stopped || !stoppedFrameId_) {
        return;
    }
    const std::uint64_t generation = frameLocalsGeneration_;
    RequestScopes(*stoppedFrameId_, [this, generation](std::vector<Scope> scopes) {
        if (generation != frameLocalsGeneration_) {
            return;
        }
        for (const Scope& scope : scopes) {
            if (scope.expensive || scope.variablesReference == 0) {
                continue; // the adapter said so; inline values are drawn every paint
            }
            RequestVariables(scope.variablesReference, [this, generation](std::vector<Variable> variables) {
                if (generation != frameLocalsGeneration_) {
                    return;
                }
                for (Variable& variable : variables) {
                    if (variable.name.empty()) {
                        continue;
                    }
                    // emplace, not assign: scopes arrive innermost first, so
                    // the first answer for a shadowed name is the right one.
                    frameLocals_.emplace(std::move(variable.name), std::move(variable.value));
                }
            });
        }
    });
}

const std::vector<Manager::ExceptionFilter>& Manager::AvailableExceptionFilters() const {
    return exceptionFilters_;
}

const std::set<std::string>& Manager::EnabledExceptionFilters() const {
    return enabledExceptionFilters_;
}

void Manager::SetExceptionBreakpointFilters(std::set<std::string> ids) {
    enabledExceptionFilters_ = std::move(ids);
    if (client_ && state_ != SessionState::Inactive) {
        SendExceptionBreakpoints();
    }
    NotifyBreakpointsChanged();
}

void Manager::RequestDataBreakpointInfo(int variablesReference, const std::string& name,
                                        std::function<void(DataBreakpointInfo)> callback) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback(DataBreakpointInfo{.description = "No stopped debug session."});
        return;
    }
    Json arguments = {{"name", name}};
    // A container reference of 0 is DAP's own "resolve this name in the
    // frame itself" -- omitted rather than sent as 0, which some adapters
    // reject outright.
    if (variablesReference > 0) {
        arguments["variablesReference"] = variablesReference;
    }
    else if (stoppedFrameId_) {
        arguments["frameId"] = *stoppedFrameId_;
    }
    const bool advertised = capabilities_.dataBreakpoints;
    client_->SendRequest("dataBreakpointInfo", std::move(arguments),
                         [callback = std::move(callback), advertised](bool success, const Json& body,
                                                                      const std::string& message) {
                             if (!success) {
                                 // The request failing is itself the answer for an
                                 // adapter that never advertised support -- say which
                                 // it was rather than surfacing a bare protocol error.
                                 DataBreakpointInfo info;
                                 info.description = message.empty() ? std::string("Adapter refused dataBreakpointInfo.") : message;
                                 if (!advertised) {
                                     info.description += " (adapter did not advertise data-breakpoint support)";
                                 }
                                 callback(std::move(info));
                                 return;
                             }
                             DataBreakpointInfo info;
                             // DAP defines "description" as the label when dataId is a
                             // string and the refusal reason when it is null, so it is
                             // read the same way on both paths.
                             info.description = body.value("description", "");
                             if (body.contains("dataId") && body["dataId"].is_string()) {
                                 info.canBreak = true;
                                 info.dataId   = body["dataId"].get<std::string>();
                             }
                             if (body.contains("accessTypes") && body["accessTypes"].is_array()) {
                                 for (const Json& accessType : body["accessTypes"]) {
                                     if (accessType.is_string()) {
                                         info.accessTypes.push_back(accessType.get<std::string>());
                                     }
                                 }
                             }
                             if (!info.canBreak && info.description.empty()) {
                                 info.description = "The adapter cannot watch that value.";
                             }
                             callback(std::move(info));
                         });
}

bool Manager::ToggleDataBreakpoint(std::string dataId, std::string description, std::string accessType) {
    const auto it = std::find_if(dataBreakpoints_.begin(), dataBreakpoints_.end(),
                                 [&dataId](const DataBreakpoint& bp) { return bp.dataId == dataId; });
    bool       nowSet;
    if (it != dataBreakpoints_.end()) {
        dataBreakpoints_.erase(it);
        nowSet = false;
    }
    else {
        dataBreakpoints_.push_back(DataBreakpoint{.dataId      = std::move(dataId),
                                                  .description = std::move(description),
                                                  .accessType  = std::move(accessType)});
        nowSet = true;
    }
    if (client_ && state_ != SessionState::Inactive) {
        SendDataBreakpoints();
    }
    NotifyBreakpointsChanged();
    return nowSet;
}

void Manager::RemoveDataBreakpointAt(std::size_t index) {
    if (index >= dataBreakpoints_.size()) {
        return;
    }
    dataBreakpoints_.erase(dataBreakpoints_.begin() + static_cast<std::ptrdiff_t>(index));
    if (client_ && state_ != SessionState::Inactive) {
        SendDataBreakpoints();
    }
    NotifyBreakpointsChanged();
}

bool Manager::SetDataBreakpointEnabled(std::size_t index, bool enabled) {
    if (index >= dataBreakpoints_.size()) {
        return false;
    }
    DataBreakpoint& bp = dataBreakpoints_[index];
    if (bp.enabled != enabled) {
        bp.enabled = enabled;
        if (!enabled) {
            bp.verified = true; // same stale-claim reset SetBreakpointEnabled documents
            bp.message.clear();
        }
        if (client_ && state_ != SessionState::Inactive) {
            SendDataBreakpoints();
        }
        NotifyBreakpointsChanged();
    }
    return true;
}

void Manager::ClearDataBreakpoints() {
    if (dataBreakpoints_.empty()) {
        return;
    }
    dataBreakpoints_.clear();
    if (client_ && state_ != SessionState::Inactive) {
        SendDataBreakpoints();
    }
    NotifyBreakpointsChanged();
}

const std::vector<Manager::DataBreakpoint>& Manager::DataBreakpoints() const {
    return dataBreakpoints_;
}

std::vector<std::size_t> Manager::BreakpointsForFile(const std::filesystem::path& path) const {
    const auto               it = breakpoints_.find(NormalizePathKey(path));
    std::vector<std::size_t> lines;
    if (it != breakpoints_.end()) {
        for (const Breakpoint& bp : it->second) {
            lines.push_back(bp.line);
        }
    }
    return lines;
}

std::map<std::string, std::vector<Manager::PersistedBreakpoint>> Manager::AllBreakpoints() const {
    std::map<std::string, std::vector<PersistedBreakpoint>> persisted;
    for (const auto& [key, breakpoints] : breakpoints_) {
        std::vector<PersistedBreakpoint>& entries = persisted[key];
        for (const Breakpoint& bp : breakpoints) {
            entries.push_back(PersistedBreakpoint{
                .line         = bp.line,
                .condition    = bp.condition,
                .logMessage   = bp.logMessage,
                .hitCondition = bp.hitCondition,
                .enabled      = bp.enabled,
            });
        }
    }
    return persisted;
}

void Manager::RestoreBreakpoints(std::map<std::string, std::vector<PersistedBreakpoint>> breakpoints) {
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
    // Every anchor was keyed by an id in the store just discarded; the buffers stay
    // tracked and rebuild their anchors from the restored lines on the next reconcile.
    for (auto& [buffer, source] : trackedSources_) {
        ReleaseAnchors(*buffer, source);
    }
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
                .id           = nextBreakpointId_++,
                .condition    = entry.condition,
                .logMessage   = entry.logMessage,
                .hitCondition = entry.hitCondition,
                .enabled      = entry.enabled,
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
    NotifyBreakpointsChanged();
}

// ---------------------------------------------------------------------------------
// Anchored breakpoint positions (dap-anchored-breakpoints)
// ---------------------------------------------------------------------------------

void Manager::TrackBuffer(text::Buffer& buffer) {
    if (!buffer.Path()) {
        return; // nothing to key breakpoints by
    }
    const std::string key      = NormalizePathKey(*buffer.Path());
    const auto        existing = trackedSources_.find(&buffer);
    if (existing == trackedSources_.end() && !breakpoints_.contains(key)) {
        return; // nothing to anchor and nothing held: don't grow the map per painted buffer
    }
    TrackedSource& source = existing != trackedSources_.end() ? existing->second : trackedSources_[&buffer];
    if (source.key != key) {
        // First sight of this buffer, or it was saved under a different name -- the old
        // file's breakpoints are not this one's.
        ReleaseAnchors(buffer, source);
        source.key = key;
    }
    if (ReconcileAnchors(buffer, source) && client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
}

void Manager::NotifyBufferClosed(text::Buffer& buffer) {
    const auto it = trackedSources_.find(&buffer);
    if (it == trackedSources_.end()) {
        return;
    }
    // One last resolve first: the lines the breakpoints keep, now that nothing is open
    // to anchor them to, should be where the anchors had got to rather than where they
    // were when the buffer was opened.
    (void)ReconcileAnchors(buffer, it->second);
    ReleaseAnchors(buffer, it->second);
    trackedSources_.erase(it);
}

void Manager::ReleaseAnchors(text::Buffer& buffer, TrackedSource& source) {
    for (const auto& [id, anchor] : source.anchors) {
        buffer.DestroyAnchor(anchor);
    }
    source.anchors.clear();
}

bool Manager::ReconcileAnchors(text::Buffer& buffer, TrackedSource& source) {
    const auto fileIt = breakpoints_.find(source.key);
    if (fileIt == breakpoints_.end()) {
        ReleaseAnchors(buffer, source);
        return false;
    }
    std::vector<Breakpoint>& breakpoints = fileIt->second;

    bool moved = false;
    for (Breakpoint& bp : breakpoints) {
        const auto anchorIt = source.anchors.find(bp.id);
        if (anchorIt == source.anchors.end()) {
            // A breakpoint toggled since the last reconcile: its line is the truth, and
            // this is where it stops being one.
            source.anchors.emplace(bp.id, buffer.CreateAnchor(LineStartOffset(buffer, bp.line)));
            continue;
        }
        const std::optional<std::size_t> offset = buffer.AnchorOffset(anchorIt->second);
        if (!offset) {
            // A barrier (a revert, a reload, an external merge) dropped it. The stored
            // line is all that is left to believe, so re-anchor to it rather than
            // guessing the breakpoint away.
            buffer.DestroyAnchor(anchorIt->second);
            anchorIt->second = buffer.CreateAnchor(LineStartOffset(buffer, bp.line));
            continue;
        }
        const std::size_t line = buffer.Content().ByteOffsetToLine(*offset) + 1; // DAP lines are 1-based
        if (line != bp.line) {
            bp.line       = line;
            bp.actualLine = 0; // the adapter's snapped location described the old line
            moved         = true;
        }
    }

    if (moved) {
        std::sort(breakpoints.begin(), breakpoints.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.line < b.line; });
        // Two breakpoints can land on one line (the lines between them were deleted, so
        // both anchors clamped to the same point). Collapse to the first, the same rule
        // RestoreBreakpoints applies to a duplicate, and drop the loser's anchor.
        const auto duplicate = std::unique(breakpoints.begin(), breakpoints.end(),
                                           [](const Breakpoint& a, const Breakpoint& b) { return a.line == b.line; });
        for (auto it = duplicate; it != breakpoints.end(); ++it) {
            if (const auto anchorIt = source.anchors.find(it->id); anchorIt != source.anchors.end()) {
                buffer.DestroyAnchor(anchorIt->second);
                source.anchors.erase(anchorIt);
            }
        }
        breakpoints.erase(duplicate, breakpoints.end());
    }

    // Anchors whose breakpoint is gone (toggled off, or collapsed above).
    for (auto it = source.anchors.begin(); it != source.anchors.end();) {
        const bool stillSet = std::any_of(breakpoints.begin(), breakpoints.end(),
                                          [id = it->first](const Breakpoint& bp) { return bp.id == id; });
        if (stillSet) {
            ++it;
            continue;
        }
        buffer.DestroyAnchor(it->second);
        it = source.anchors.erase(it);
    }
    return moved;
}

void Manager::ReconcileAllTrackedSources(bool pushToAdapter) {
    for (auto& [buffer, source] : trackedSources_) {
        const bool moved = ReconcileAnchors(*buffer, source);
        if (moved && pushToAdapter && client_ && state_ != SessionState::Inactive) {
            SendBreakpointsForFile(source.key);
        }
    }
}

void Manager::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    if (client_) {
        client_->ExpireStaleRequests(maxAge);
    }
}

std::string Manager::StartOrContinue(const std::string& language) {
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
    if (!LaunchConfig(language)) {
        return "No launch configuration for " + language + " (ned/set-dap-launch).";
    }
    return BeginSession(language, /*attach=*/false);
}

std::string Manager::Attach(const std::string& language) {
    if (state_ != SessionState::Inactive) {
        return "Debug session already running.";
    }
    if (!AttachConfig(language)) {
        return "No attach configuration for " + language + " (ned/set-dap-attach).";
    }
    return BeginSession(language, /*attach=*/true);
}

std::string Manager::BeginSession(const std::string& language, bool attach) {
    capabilities_ = Capabilities{}; // fresh adapter, fresh capabilities -- see the header's own doc comment
    exceptionFilters_.clear();
    enabledExceptionFilters_.clear();
    isAttach_ = attach;
    // Debugging wishlist: watch-history sparkline -- a fresh session's
    // values aren't comparable to a prior run's (WatchHistoryAt's own doc
    // comment), so every watch starts this session with an empty history.
    watchHistory_.assign(watches_.size(), {});

    if (!client_) {
        const auto argv = AdapterCommand(language);
        if (!argv) {
            return "No debug adapter configured for " + language + " (ned/set-dap-adapter).";
        }
        try {
            client_ = std::make_unique<Client>(*argv, eventLoop_);
        }
        catch (const std::exception& e) {
            client_.reset();
            return std::string("Failed to start debug adapter: ") + e.what();
        }
    }
    // else: a client injected via SetClientForTesting — run the same
    // handshake against it.

    language_ = language;
    SetState(SessionState::Starting);
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
                             if (body.contains("supportsGotoTargetsRequest") && body["supportsGotoTargetsRequest"].is_boolean()) {
                                 capabilities_.gotoTargets = body["supportsGotoTargetsRequest"].get<bool>();
                             }
                             if (body.contains("supportsStepBack") && body["supportsStepBack"].is_boolean()) {
                                 capabilities_.stepBack = body["supportsStepBack"].get<bool>();
                             }
                             if (body.contains("supportsDataBreakpoints") && body["supportsDataBreakpoints"].is_boolean()) {
                                 capabilities_.dataBreakpoints = body["supportsDataBreakpoints"].get<bool>();
                             }
                             if (body.contains("supportsCompletionsRequest") &&
                                 body["supportsCompletionsRequest"].is_boolean()) {
                                 capabilities_.completions = body["supportsCompletionsRequest"].get<bool>();
                             }
                             if (body.contains("supportsStepInTargetsRequest") &&
                                 body["supportsStepInTargetsRequest"].is_boolean()) {
                                 capabilities_.stepInTargets = body["supportsStepInTargetsRequest"].get<bool>();
                             }
                             if (body.contains("supportsBreakpointLocationsRequest") &&
                                 body["supportsBreakpointLocationsRequest"].is_boolean()) {
                                 capabilities_.breakpointLocations = body["supportsBreakpointLocationsRequest"].get<bool>();
                             }
                             if (body.contains("supportsSetExpression") && body["supportsSetExpression"].is_boolean()) {
                                 capabilities_.setExpression = body["supportsSetExpression"].get<bool>();
                             }
                             if (body.contains("supportsModulesRequest") && body["supportsModulesRequest"].is_boolean()) {
                                 capabilities_.modules = body["supportsModulesRequest"].get<bool>();
                             }
                             if (body.contains("supportsLoadedSourcesRequest") &&
                                 body["supportsLoadedSourcesRequest"].is_boolean()) {
                                 capabilities_.loadedSources = body["supportsLoadedSourcesRequest"].get<bool>();
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
                                 NotifyBreakpointsChanged(); // a listing's exception-filter section just came into existence
                             }
                             SendLaunchOrAttach();
                         });
    return std::string("Starting debug session (") + language + ")...";
}

void Manager::SendLaunchOrAttach() {
    const auto config    = isAttach_ ? AttachConfig(language_) : LaunchConfig(language_);
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
            SetState(SessionState::Running);
        }
    });
}

void Manager::WireClient(Client& client) {
    client.SetEventHandler("initialized", [this](const Json&) { HandleInitializedEvent(); });
    client.SetEventHandler("stopped", [this](const Json& body) { HandleStoppedEvent(body); });
    client.SetEventHandler("terminated", [this](const Json&) { EndSession("Debug session terminated."); });
    client.SetEventHandler("exited", [this](const Json& body) {
        const int exitCode = body.value("exitCode", 0);
        EndSession("Debuggee exited (code " + std::to_string(exitCode) + ").");
    });
    client.SetOnDisconnected([this](std::string reason) { EndSession("Debug adapter disconnected: " + std::move(reason)); });
}

void Manager::HandleInitializedEvent() {
    // Anything edited since the last reconcile (a buffer in an unfocused pane, a
    // project-wide replace) is caught here, before a single line goes out.
    ReconcileAllTrackedSources(/*pushToAdapter=*/false);
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

void Manager::SendFunctionBreakpoints() {
    Json breakpointsJson = Json::array();
    for (const FunctionBreakpoint& bp : functionBreakpoints_) {
        if (!bp.enabled) {
            continue; // "not sent" is what disabled means on the wire
        }
        breakpointsJson.push_back(Json{{"name", bp.name}});
    }
    client_->SendRequest("setFunctionBreakpoints", Json{{"breakpoints", std::move(breakpointsJson)}},
                         [](bool, const Json&, const std::string&) {
                             // No per-entry "verified" tracking for function
                             // breakpoints yet (documented cut, ROADMAP.md) --
                             // same "fire and let a failure surface via the
                             // session ending" shape as configurationDone above.
                         });
}

void Manager::SendExceptionBreakpoints() {
    Json filtersJson = Json::array();
    for (const std::string& id : enabledExceptionFilters_) {
        filtersJson.push_back(id);
    }
    client_->SendRequest("setExceptionBreakpoints", Json{{"filters", std::move(filtersJson)}},
                         [](bool, const Json&, const std::string&) {
                             // Same fire-and-forget shape as setFunctionBreakpoints above.
                         });
}

void Manager::SendDataBreakpoints() {
    Json                     breakpointsJson = Json::array();
    std::vector<std::string> sentIds;
    for (const DataBreakpoint& bp : dataBreakpoints_) {
        if (!bp.enabled) {
            continue; // "not sent" is what disabled means on the wire
        }
        Json entry = Json{{"dataId", bp.dataId}};
        if (!bp.accessType.empty()) {
            entry["accessType"] = bp.accessType;
        }
        breakpointsJson.push_back(std::move(entry));
        sentIds.push_back(bp.dataId);
    }
    client_->SendRequest("setDataBreakpoints", Json{{"breakpoints", std::move(breakpointsJson)}},
                         [this, sentIds = std::move(sentIds)](bool success, const Json& body, const std::string&) {
                             if (!success || !body.contains("breakpoints") || !body["breakpoints"].is_array()) {
                                 return;
                             }
                             // The response array pairs with the REQUEST's order, which
                             // is not necessarily the store's any more -- a toggle
                             // while this was in flight already sent its own
                             // setDataBreakpoints. Matching back by the id that was
                             // actually sent costs nothing and cannot mispair, unlike
                             // setBreakpoints' positional match (where the key, a line,
                             // isn't echoed).
                             const Json& results = body["breakpoints"];
                             for (std::size_t i = 0; i < sentIds.size() && i < results.size(); ++i) {
                                 const auto it = std::find_if(dataBreakpoints_.begin(), dataBreakpoints_.end(),
                                                              [&](const DataBreakpoint& bp) { return bp.dataId == sentIds[i]; });
                                 if (it == dataBreakpoints_.end()) {
                                     continue; // removed before the response landed
                                 }
                                 if (results[i].contains("verified") && results[i]["verified"].is_boolean()) {
                                     it->verified = results[i]["verified"].get<bool>();
                                 }
                                 it->message = results[i].value("message", "");
                             }
                             NotifyBreakpointsChanged();
                         });
}

void Manager::SendBreakpointsForFile(const std::string& pathKey) {
    Json                       breakpointsJson = Json::array();
    std::vector<std::uint64_t> sentIds;
    if (const auto it = breakpoints_.find(pathKey); it != breakpoints_.end()) {
        for (const Breakpoint& bp : it->second) {
            if (!bp.enabled) {
                continue; // "not sent" is what disabled means on the wire
            }
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
            sentIds.push_back(bp.id);
        }
    }
    client_->SendRequest("setBreakpoints",
                         Json{
                             {"source", Json{{"path", pathKey}}},
                             {"breakpoints", std::move(breakpointsJson)},
                         },
                         [this, pathKey, sentIds = std::move(sentIds)](bool success, const Json& body, const std::string&) {
                             // "verified" IS tracked now -- it dims the gutter glyph
                             // rather than being dropped on the floor. DAP round 4: the
                             // adapter's own snapped "line" per entry is tracked too
                             // (actualLine) -- the gutter shows it in place of the
                             // requested line when it differs; editing operations still
                             // address the requested line (see Breakpoint::actualLine's
                             // own doc comment).
                             //
                             // debug-panel: matched back by Breakpoint::id rather than
                             // by store position. The response pairs with the REQUEST's
                             // order, which is no longer the store's -- a disabled
                             // breakpoint occupies a slot here and none on the wire --
                             // and this is also the pairing that survives a toggle
                             // landing while the request was in flight, the same
                             // reasoning SendDataBreakpoints' own dataId match records.
                             if (!success || !body.contains("breakpoints") || !body["breakpoints"].is_array()) {
                                 return;
                             }
                             const auto it = breakpoints_.find(pathKey);
                             if (it == breakpoints_.end()) {
                                 return; // toggled off again before the response landed
                             }
                             const Json& results = body["breakpoints"];
                             bool        changed = false;
                             for (std::size_t i = 0; i < sentIds.size() && i < results.size(); ++i) {
                                 const auto bp = std::find_if(it->second.begin(), it->second.end(),
                                                              [id = sentIds[i]](const Breakpoint& candidate) { return candidate.id == id; });
                                 if (bp == it->second.end()) {
                                     continue; // removed before the response landed
                                 }
                                 if (results[i].contains("verified") && results[i]["verified"].is_boolean()) {
                                     bp->verified = results[i]["verified"].get<bool>();
                                 }
                                 if (results[i].contains("line") && results[i]["line"].is_number_integer()) {
                                     bp->actualLine = static_cast<std::size_t>(std::max(results[i]["line"].get<int>(), 1));
                                 }
                                 changed = true;
                             }
                             if (changed) {
                                 NotifyBreakpointsChanged();
                             }
                         });
}

void Manager::HandleStoppedEvent(const Json& body) {
    // Run-to-cursor's temporary breakpoint (if any) is cleared on the very
    // next stop for any reason -- only one continue was ever issued for it.
    ClearPendingRunToCursor(/*pushToAdapter=*/true);
    SetState(SessionState::Stopped);
    stoppedThreadId_ = body.value("threadId", 1);
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
                             RefreshWatchHistory(); // stoppedFrameId_ is set by now, same scoping Evaluate itself uses
                             RefreshFrameLocals();  // same reason, same moment
                             if (onStopped_) {
                                 onStopped_(info);
                             }
                         });
}

void Manager::RefreshWatchHistory() {
    for (std::size_t i = 0; i < watches_.size(); ++i) {
        Evaluate(
            watches_[i],
            [this, i](bool success, std::string text) {
                if (!success || i >= watchHistory_.size()) {
                    return; // failed evaluation, or the watch was removed while this request was in flight
                }
                double value = 0.0;
                if (!TryParseNumeric(text, value)) {
                    return; // non-numeric result -- skipped, not recorded as a gap
                }
                std::vector<double>& history = watchHistory_[i];
                history.push_back(value);
                if (history.size() > kMaxWatchHistoryPoints) {
                    history.erase(history.begin());
                }
            },
            "watch");
    }
}

std::string Manager::Pause() {
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

std::string Manager::StopSession() {
    if (state_ == SessionState::Inactive) {
        return "No debug session.";
    }
    // Best-effort polite disconnect; teardown below must not depend on the
    // adapter answering (or even still being alive to write to).
    // async-write-queue follow-up: PrepareForGracefulShutdown must be called
    // before this SendRequest -- without it, EndSession destroying client_
    // right below could race the write thread's own stop and silently drop
    // this "disconnect" frame before it ever reaches the wire (see
    // Client.h's own header comment).
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

void Manager::MarkResumed() {
    SetState(SessionState::Running);
    currentStop_.reset();
    stoppedFrameId_.reset();
    focusedThreadId_.reset();
    // debug-panel (inline values): a running debuggee's locals are not
    // stale, they are meaningless -- the frame they belonged to may not
    // exist any more. Bumping the generation also drops any response still
    // in flight from the stop just left.
    ++frameLocalsGeneration_;
    frameLocals_.clear();
}

std::string Manager::SendStep(const std::string& command, const std::string& label) {
    return SendStepWith(command, label, Json::object());
}

std::string Manager::SendStepWith(const std::string& command, const std::string& label, Json extraArguments) {
    if (state_ != SessionState::Stopped) {
        return "Not stopped (nothing to step).";
    }
    Json arguments = Json{{"threadId", CurrentThreadId()}};
    for (const auto& [key, value] : extraArguments.items()) {
        arguments[key] = value;
    }
    client_->SendRequest(command, std::move(arguments),
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

std::string Manager::StepOver() {
    return SendStep("next", "Stepping over");
}

std::string Manager::StepInto() {
    return SendStep("stepIn", "Stepping into");
}

void Manager::RequestCompletions(const std::string& text, int column, std::function<void(std::vector<Completion>)> callback) {
    if (!client_ || state_ != SessionState::Stopped || !capabilities_.completions) {
        callback({});
        return;
    }
    Json arguments = {{"text", text}, {"column", column}};
    if (stoppedFrameId_) {
        arguments["frameId"] = *stoppedFrameId_; // the same scoping Evaluate uses
    }
    client_->SendRequest("completions", std::move(arguments),
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<Completion> completions;
                             if (success && body.contains("targets") && body["targets"].is_array()) {
                                 for (const Json& targetJson : body["targets"]) {
                                     Completion completion;
                                     completion.label = targetJson.value("label", "");
                                     if (completion.label.empty()) {
                                         continue;
                                     }
                                     completion.text   = targetJson.value("text", completion.label);
                                     completion.type   = targetJson.value("type", "");
                                     completion.start  = targetJson.value("start", -1);
                                     completion.length = targetJson.value("length", -1);
                                     completions.push_back(std::move(completion));
                                 }
                             }
                             callback(std::move(completions));
                         });
}

std::string Manager::StepIntoTarget(int targetId) {
    return SendStepWith("stepIn", "Stepping into", Json{{"targetId", targetId}});
}

void Manager::RequestStepInTargets(int frameId, std::function<void(std::vector<StepInTarget>)> callback) {
    if (!client_ || state_ != SessionState::Stopped || !capabilities_.stepInTargets) {
        callback({});
        return;
    }
    client_->SendRequest("stepInTargets", Json{{"frameId", frameId}},
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<StepInTarget> targets;
                             if (success && body.contains("targets") && body["targets"].is_array()) {
                                 for (const Json& targetJson : body["targets"]) {
                                     StepInTarget target;
                                     target.id    = targetJson.value("id", 0);
                                     target.label = targetJson.value("label", "");
                                     if (target.label.empty()) {
                                         continue;
                                     }
                                     targets.push_back(std::move(target));
                                 }
                             }
                             callback(std::move(targets));
                         });
}

std::string Manager::StepOut() {
    return SendStep("stepOut", "Stepping out");
}

std::string Manager::ReverseContinue() {
    std::string status = SendStep("reverseContinue", "Reverse-continuing");
    if (!capabilities_.stepBack && status == "Reverse-continuing...") {
        status += " (adapter did not advertise reverse-debugging support -- may be ignored)";
    }
    return status;
}

std::string Manager::StepBack() {
    std::string status = SendStep("stepBack", "Stepping back");
    if (!capabilities_.stepBack && status == "Stepping back...") {
        status += " (adapter did not advertise reverse-debugging support -- may be ignored)";
    }
    return status;
}

std::string Manager::RunToCursor(const std::filesystem::path& path, std::size_t line) {
    if (state_ != SessionState::Stopped) {
        return "Not stopped (nothing to run to cursor from).";
    }
    const std::string key        = NormalizePathKey(path);
    const auto        it         = breakpoints_.find(key);
    const bool        alreadySet = it != breakpoints_.end() &&
                                   std::any_of(it->second.begin(), it->second.end(), [line](const Breakpoint& bp) { return bp.line == line; });
    if (!alreadySet) {
        ToggleBreakpoint(path, line); // pushes setBreakpoints immediately (state_ != Inactive)
        const auto created = breakpoints_.find(key);
        if (created != breakpoints_.end()) {
            const auto bp = std::find_if(created->second.begin(), created->second.end(),
                                         [line](const Breakpoint& candidate) { return candidate.line == line; });
            if (bp != created->second.end()) {
                pendingRunToCursor_ = std::make_pair(key, bp->id);
            }
        }
    }
    client_->SendRequest("continue", Json{{"threadId", CurrentThreadId()}},
                         [this](bool success, const Json&, const std::string& message) {
                             if (success) {
                                 MarkResumed();
                             }
                             else {
                                 EndSession("continue failed: " + message);
                             }
                         });
    return "Running to cursor...";
}

void Manager::ClearPendingRunToCursor(bool pushToAdapter) {
    if (!pendingRunToCursor_) {
        return;
    }
    const auto [key, id] = *pendingRunToCursor_;
    pendingRunToCursor_.reset();
    const auto it = breakpoints_.find(key);
    if (it == breakpoints_.end()) {
        return; // toggled off some other way already
    }
    const auto lineIt = std::find_if(it->second.begin(), it->second.end(), [id](const Breakpoint& bp) { return bp.id == id; });
    if (lineIt == it->second.end()) {
        return;
    }
    it->second.erase(lineIt);
    if (it->second.empty()) {
        breakpoints_.erase(it);
    }
    if (pushToAdapter && client_ && state_ != SessionState::Inactive) {
        SendBreakpointsForFile(key);
    }
}

void Manager::JumpToLine(const std::filesystem::path& path, std::size_t line, std::function<void(bool, std::string)> callback) {
    if (state_ != SessionState::Stopped) {
        callback(false, "Not stopped (nothing to jump from).");
        return;
    }
    const int threadId = CurrentThreadId();
    client_->SendRequest(
        "gotoTargets", Json{{"source", Json{{"path", path.string()}}}, {"line", line}},
        [this, threadId, callback](bool success, const Json& body, const std::string& message) {
            if (!success) {
                std::string status = "gotoTargets failed: " + message;
                if (!capabilities_.gotoTargets) {
                    status += " (adapter did not advertise jump-to-line support)";
                }
                callback(false, status);
                return;
            }
            if (!body.contains("targets") || !body["targets"].is_array() || body["targets"].empty()) {
                callback(false, "No jump target available at that line.");
                return;
            }
            const Json& firstTarget = body["targets"][0];
            if (!firstTarget.contains("id") || !firstTarget["id"].is_number_integer()) {
                callback(false, "Adapter returned a malformed jump target.");
                return;
            }
            const int targetId = firstTarget["id"].get<int>();
            client_->SendRequest("goto", Json{{"threadId", threadId}, {"targetId", targetId}},
                                 [callback](bool success, const Json&, const std::string& message) {
                                     if (!success) {
                                         callback(false, "goto failed: " + message);
                                         return;
                                     }
                                     // The new position arrives via the following `stopped`
                                     // event (reason "goto"), same as continue/step -- nothing
                                     // else to update here.
                                     callback(true, "Jumped to line.");
                                 });
        });
}

std::string Manager::RestartFrame(int frameId) {
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

std::optional<std::pair<std::string, std::size_t>> Manager::CurrentStopKeyAndLine() const {
    return currentStop_;
}

std::vector<std::size_t> Manager::BreakpointLinesForKey(const std::string& key) const {
    std::vector<std::size_t> lines;
    const auto               it = breakpoints_.find(key);
    if (it != breakpoints_.end()) {
        for (const Breakpoint& bp : it->second) {
            lines.push_back(bp.line);
        }
    }
    return lines;
}

std::vector<Manager::Breakpoint> Manager::BreakpointsForKey(const std::string& key) const {
    const auto it = breakpoints_.find(key);
    return it != breakpoints_.end() ? it->second : std::vector<Breakpoint>{};
}

int Manager::CurrentThreadId() const {
    return focusedThreadId_.value_or(stoppedThreadId_);
}

void Manager::RequestModules(std::function<void(std::vector<Module>)> callback) {
    if (!client_ || state_ == SessionState::Inactive || state_ == SessionState::Starting || !capabilities_.modules) {
        callback({});
        return;
    }
    client_->SendRequest("modules", Json::object(), [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
        std::vector<Module> modules;
        if (success && body.contains("modules") && body["modules"].is_array()) {
            for (const Json& moduleJson : body["modules"]) {
                Module module;
                // DAP allows an id to be a number or a string; neither is
                // used as a key here, only shown, so it is normalized to
                // text rather than branched on everywhere downstream.
                if (moduleJson.contains("id")) {
                    module.id = moduleJson["id"].is_string() ? moduleJson["id"].get<std::string>() : moduleJson["id"].dump();
                }
                module.name         = moduleJson.value("name", "");
                module.path         = moduleJson.value("path", "");
                module.symbolStatus = moduleJson.value("symbolStatus", "");
                module.isUserCode   = moduleJson.value("isUserCode", false);
                if (module.name.empty()) {
                    continue;
                }
                modules.push_back(std::move(module));
            }
        }
        callback(std::move(modules));
    });
}

void Manager::RequestLoadedSources(std::function<void(std::vector<LoadedSource>)> callback) {
    if (!client_ || state_ == SessionState::Inactive || state_ == SessionState::Starting || !capabilities_.loadedSources) {
        callback({});
        return;
    }
    client_->SendRequest("loadedSources", Json::object(),
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<LoadedSource> sources;
                             if (success && body.contains("sources") && body["sources"].is_array()) {
                                 for (const Json& sourceJson : body["sources"]) {
                                     LoadedSource source;
                                     source.name = sourceJson.value("name", "");
                                     if (sourceJson.contains("path") && sourceJson["path"].is_string()) {
                                         source.path = std::filesystem::path(sourceJson["path"].get<std::string>());
                                         if (source.name.empty()) {
                                             source.name = source.path->filename().string();
                                         }
                                     }
                                     if (source.name.empty()) {
                                         continue;
                                     }
                                     sources.push_back(std::move(source));
                                 }
                             }
                             callback(std::move(sources));
                         });
}

void Manager::RequestStackTrace(std::function<void(std::vector<StackFrame>)> callback, int threadId) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback({});
        return;
    }
    client_->SendRequest("stackTrace",
                         Json{{"threadId", threadId != 0 ? threadId : CurrentThreadId()}, {"startFrame", 0}, {"levels", 20}},
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

void Manager::RequestScopes(int frameId, std::function<void(std::vector<Scope>)> callback) {
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
                                         .expensive          = scopeJson.value("expensive", false),
                                     });
                                 }
                             }
                             callback(std::move(scopes));
                         });
}

void Manager::RequestVariables(int variablesReference, std::function<void(std::vector<Variable>)> callback, bool hex) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback({});
        return;
    }
    Json arguments = {{"variablesReference", variablesReference}};
    if (hex) {
        arguments["format"] = Json{{"hex", true}};
    }
    client_->SendRequest("variables", std::move(arguments),
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
                                         .evaluateName       = variableJson.value("evaluateName", ""),
                                     });
                                 }
                             }
                             callback(std::move(variables));
                         });
}

void Manager::RequestDisassembly(const std::string& memoryReference, long instructionOffset, int instructionCount,
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

void Manager::RequestMemory(const std::string& memoryReference, long offset, std::size_t count,
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

void Manager::Evaluate(const std::string& expression, std::function<void(bool, std::string)> callback, std::string context,
                          bool hex) {
    if (!client_ || state_ == SessionState::Inactive || state_ == SessionState::Starting) {
        callback(false, "No debug session.");
        return;
    }
    Json arguments = {{"expression", expression}, {"context", std::move(context)}};
    if (stoppedFrameId_) {
        arguments["frameId"] = *stoppedFrameId_;
    }
    if (hex) {
        arguments["format"] = Json{{"hex", true}};
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

void Manager::EvaluateWithReference(const std::string& expression, std::function<void(EvaluateResult)> callback,
                                       std::string context) {
    if (!client_ || state_ == SessionState::Inactive || state_ == SessionState::Starting) {
        callback(EvaluateResult{});
        return;
    }
    Json arguments = {{"expression", expression}, {"context", std::move(context)}};
    if (stoppedFrameId_) {
        arguments["frameId"] = *stoppedFrameId_;
    }
    client_->SendRequest("evaluate", std::move(arguments),
                         [callback = std::move(callback)](bool success, const Json& body, const std::string& message) {
                             EvaluateResult result;
                             result.success            = success;
                             result.text               = success ? body.value("result", "") : message;
                             result.variablesReference = success ? body.value("variablesReference", 0) : 0;
                             callback(std::move(result));
                         });
}

void Manager::AddWatch(std::string expression) {
    watches_.push_back(std::move(expression));
    watchHistory_.emplace_back();
}

void Manager::RemoveWatchAt(std::size_t index) {
    if (index < watches_.size()) {
        watches_.erase(watches_.begin() + static_cast<std::ptrdiff_t>(index));
        watchHistory_.erase(watchHistory_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

const std::vector<std::string>& Manager::Watches() const {
    return watches_;
}

void Manager::RestoreWatches(std::vector<std::string> watches) {
    watches_ = std::move(watches);
    watchHistory_.assign(watches_.size(), {});
}

const std::vector<double>& Manager::WatchHistoryAt(std::size_t index) const {
    static const std::vector<double> kEmpty;
    return index < watchHistory_.size() ? watchHistory_[index] : kEmpty;
}

void Manager::RequestThreads(std::function<void(std::vector<Thread>)> callback) {
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

void Manager::SelectThread(int threadId, std::function<void(bool)> callback) {
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

void Manager::RequestBreakpointLocations(const std::filesystem::path& path, std::size_t line, std::size_t endLine,
                                         std::function<void(std::vector<std::size_t>)> callback) {
    if (!client_ || state_ == SessionState::Inactive || state_ == SessionState::Starting ||
        !capabilities_.breakpointLocations) {
        callback({});
        return;
    }
    client_->SendRequest("breakpointLocations",
                         Json{
                             {"source", Json{{"path", NormalizePathKey(path)}}},
                             {"line", line},
                             {"endLine", std::max(endLine, line)},
                         },
                         [callback = std::move(callback)](bool success, const Json& body, const std::string&) {
                             std::vector<std::size_t> lines;
                             if (success && body.contains("breakpoints") && body["breakpoints"].is_array()) {
                                 for (const Json& location : body["breakpoints"]) {
                                     if (location.contains("line") && location["line"].is_number_integer()) {
                                         lines.push_back(static_cast<std::size_t>(std::max(location["line"].get<int>(), 1)));
                                     }
                                 }
                             }
                             std::sort(lines.begin(), lines.end());
                             lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
                             callback(std::move(lines));
                         });
}

bool Manager::SupportsSetVariable() const {
    return capabilities_.setVariable;
}

void Manager::SetExpression(const std::string& expression, const std::string& value,
                            std::function<void(SetVariableResult)> callback) {
    if (!client_ || state_ != SessionState::Stopped) {
        callback(SetVariableResult{.success = false, .errorMessage = "No debug session."});
        return;
    }
    if (!capabilities_.setExpression) {
        callback(SetVariableResult{.success      = false,
                                   .errorMessage = "This adapter does not support assigning to an expression."});
        return;
    }
    Json arguments = {{"expression", expression}, {"value", value}};
    if (stoppedFrameId_) {
        arguments["frameId"] = *stoppedFrameId_; // the same scoping Evaluate uses
    }
    client_->SendRequest("setExpression", std::move(arguments),
                         [callback = std::move(callback)](bool success, const Json& body, const std::string& message) {
                             SetVariableResult result;
                             result.success = success;
                             if (success) {
                                 result.value              = body.value("value", "");
                                 result.type               = body.value("type", "");
                                 result.variablesReference = body.value("variablesReference", 0);
                             }
                             else {
                                 result.errorMessage = message;
                             }
                             callback(std::move(result));
                         });
}

void Manager::SetVariable(int variablesReference, const std::string& name, const std::string& value,
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
                                 result.type               = body.value("type", "");
                                 result.variablesReference = body.value("variablesReference", 0);
                             }
                             else {
                                 result.errorMessage = message;
                             }
                             callback(std::move(result));
                         });
}

void Manager::EndSession(std::string reason) {
    if (state_ == SessionState::Inactive) {
        return; // e.g. disconnect EOF arriving after an explicit StopSession already tore down
    }
    // The session is ending before the next stop ever arrived to clear this
    // itself -- erase it locally so it doesn't leak into the next session as
    // a permanent breakpoint the user never actually asked to keep. No live
    // adapter left worth telling (client_ is torn down just below).
    ClearPendingRunToCursor(/*pushToAdapter=*/false);
    SetState(SessionState::Inactive);
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
    // Data breakpoints go with them: a dataId is minted by this adapter for
    // this run, so keeping one would arm the next session against an id it
    // never issued -- see ToggleDataBreakpoint's own doc comment.
    dataBreakpoints_.clear();
    frameLocals_.clear();
    ++frameLocalsGeneration_;
    isAttach_ = false;
    // lsp-use-after-free follow-up: client_ used to move into retired_ here
    // instead of destroying in place, deferring to the next StartOrContinue
    // ("safe here: nothing of a previous session is on the stack" -- true,
    // but confirmed live elsewhere in this codebase that deferring isn't
    // actually what makes this safe: Client's own identical pattern still
    // raced a periodic tick against a background thread's own Post()ed
    // callback for the same object). The real fix now lives in Client
    // itself (alive_, see Client.h's header comment) -- a stray Post()ed
    // callback safely no-ops instead of touching freed memory regardless of
    // when this destroys the object, so plain immediate destruction is safe.
    client_.reset();
    // The exception-filter and data-breakpoint stores just emptied, and a
    // standing listing shows both -- same staleness a toggle would cause.
    NotifyBreakpointsChanged();
    if (onSessionEnded_) {
        onSessionEnded_(std::move(reason));
    }
}

void Manager::SetOnStopped(std::function<void(const StoppedInfo&)> handler) {
    onStopped_ = std::move(handler);
}

void Manager::SetOnSessionEnded(std::function<void(std::string)> handler) {
    onSessionEnded_ = std::move(handler);
}

Client& Manager::SetClientForTesting(std::unique_ptr<Client> client) {
    client_ = std::move(client);
    return *client_;
}

} // namespace ned::editor::dap
