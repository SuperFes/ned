#include "Manager.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include "Config.h"
#include "Editor/BackgroundActivity.h"
#include "Editor/Backup.h"
#include "Editor/Mcp/BridgeServer.h"
#include "Editor/Mcp/BridgeSetting.h"
#include "Editor/Project/Root.h"
#include "Editor/WrapOverrides.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/FilePreservation.h"
#include "Text/Utf8.h"

namespace ned::editor::acp {

namespace {

    // A field that is missing, null or of another type reads as `fallback`:
    // an agent's JSON is input, and nlohmann's value() throws on a present
    // key of the wrong type (a null messageId in a replayed session, seen
    // live).
    std::string StringField(const Json& object, const char* key, std::string fallback = {}) {
        if (!object.is_object()) {
            return fallback;
        }
        const auto it = object.find(key);
        return it != object.end() && it->is_string() ? it->get<std::string>() : std::move(fallback);
    }

    bool BoolField(const Json& object, const char* key) {
        if (!object.is_object()) {
            return false;
        }
        const auto it = object.find(key);
        return it != object.end() && it->is_boolean() && it->get<bool>();
    }

    std::string OutputBufferName(std::string_view agentName) {
        return "*acp: " + std::string(agentName) + "*";
    }

    // diff-preview-line-diff-utility follow-up: factored out of
    // PushOrUpdateToolCall so the session/request_permission handler below
    // (which sees the same {type: "diff", path, oldText, newText} shape
    // inside a pending toolCall's own "content", not a tool_call_update) can
    // parse it identically instead of duplicating the loop. Returns nullopt
    // when content has no "diff"-typed item at all -- callers must not
    // conflate that with "diff cleared," matching this file's own
    // "absence means unchanged" convention elsewhere.
    struct DiffContent {
        std::string oldText;
        std::string newText;
    };
    std::optional<DiffContent> ExtractDiffContent(const Json& content) {
        if (!content.is_array()) {
            return std::nullopt;
        }
        for (const Json& item : content) {
            if (item.is_object() && StringField(item, "type", std::string()) == "diff") {
                return DiffContent{StringField(item, "oldText", std::string()), StringField(item, "newText", std::string())};
            }
        }
        return std::nullopt;
    }

    // ACP MCP tool-server bridge, slice 1. This process's own executable
    // path, for the mcpServers "command" the agent spawns as
    // `--mcp-stdio-relay` -- the exact same "/proc/self/exe is a magic
    // symlink tracking the specific inode this process actually is" idiom
    // BrokerMain.cpp's own self-identity check already uses. Throws if
    // unreadable (StartSession's own try/catch around this whole block
    // degrades to no MCP tools rather than propagating).
    std::filesystem::path SelfExecutablePath() {
        std::error_code       ec;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec || path.empty()) {
            throw std::runtime_error("cannot resolve /proc/self/exe: " + ec.message());
        }
        return path;
    }

    // Mode-line spinner name for a prompt in flight -- see Manager::
    // PromptInFlight's doc comment. String, not string_view, to match
    // BackgroundActivity's own std::string parameters.
    const std::string kAcpActivity{"ACP"};

    // Sibling-temp-file + rename, the same atomic-write shape
    // ProjectReplace.cpp's own ReplaceMatches uses -- including its
    // file-attribute-preservation follow-up, since this writes the user's
    // own files (on an agent's behalf, which is all the more reason not to
    // quietly strip a mode bit or break a link). See
    // Text/FilePreservation.h.
    void WriteFileAtomically(const std::filesystem::path& path, const std::string& content) {
        const std::filesystem::path         target     = text::ResolveSaveTarget(path);
        const text::PreservedFileAttributes attributes = text::CaptureFileAttributes(target);

        if (text::ShouldWriteInPlace(attributes)) {
            std::ofstream output(target, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot open " + target.string() + " for writing");
            }
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!output) {
                throw std::runtime_error("write failed for " + target.string());
            }
            return;
        }

        std::filesystem::path tempPath = target;
        tempPath += ".ned-tmp";
        {
            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot open " + tempPath.string() + " for writing");
            }
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!output) {
                throw std::runtime_error("write failed for " + tempPath.string());
            }
        }

        text::ApplyFileAttributes(tempPath, attributes);

        std::error_code ec;
        std::filesystem::rename(tempPath, target, ec);
        if (ec) {
            throw std::runtime_error("rename failed for " + target.string() + ": " + ec.message());
        }
    }

    // fs/read_text_file's optional line/limit narrowing -- startLine is
    // 1-based per the ACP spec (matching every other 1-based line convention
    // already in this codebase); limit < 0 means "no limit".
    std::string SliceLines(const std::string& content, int startLine, int limit) {
        std::vector<std::string_view> lines;
        std::size_t                   pos = 0;
        while (pos <= content.size()) {
            const std::size_t newlinePos = content.find('\n', pos);
            if (newlinePos == std::string::npos) {
                lines.push_back(std::string_view(content).substr(pos));
                break;
            }
            lines.push_back(std::string_view(content).substr(pos, newlinePos - pos));
            pos = newlinePos + 1;
        }
        const std::size_t startIndex = startLine > 1 ? static_cast<std::size_t>(startLine - 1) : 0;
        std::string       result;
        for (std::size_t i = startIndex; i < lines.size() && (limit < 0 || static_cast<int>(i - startIndex) < limit); ++i) {
            result += lines[i];
            result += '\n';
        }
        return result;
    }

    // Enough of a tool's output to read in the panel without holding a
    // whole build log per tool call.
    constexpr std::size_t kMaxToolOutputBytes = 16 * 1024;
    constexpr std::size_t kMaxToolInputBytes  = 200;

    std::vector<Manager::ToolLocation> ParseLocations(const Json& locations) {
        std::vector<Manager::ToolLocation> result;
        if (!locations.is_array()) {
            return result;
        }
        for (const Json& location : locations) {
            if (!location.is_object() || !location.contains("path") || !location["path"].is_string()) {
                continue;
            }
            Manager::ToolLocation parsed{.path = location["path"].get<std::string>(), .line = std::nullopt};
            if (location.contains("line") && location["line"].is_number_unsigned()) {
                parsed.line = location["line"].get<std::size_t>();
            }
            result.push_back(std::move(parsed));
        }
        return result;
    }

    // One line describing what a tool was asked to do. rawInput is the
    // agent's own tool-specific shape, so this only knows the field names
    // common coding tools use.
    std::string SummarizeToolInput(const Json& rawInput) {
        std::string summary;
        if (rawInput.is_string()) {
            summary = rawInput.get<std::string>();
        }
        else if (rawInput.is_object()) {
            for (const char* key : {"command", "file_path", "path", "notebook_path", "pattern", "url", "query"}) {
                if (rawInput.contains(key) && rawInput[key].is_string()) {
                    summary = rawInput[key].get<std::string>();
                    if (std::string_view(key) == "command") {
                        summary = "$ " + summary;
                    }
                    break;
                }
            }
        }
        if (const std::size_t newline = summary.find('\n'); newline != std::string::npos) {
            summary = summary.substr(0, newline) + " ...";
        }
        if (summary.size() > kMaxToolInputBytes) {
            summary = summary.substr(0, text::SnapDownToCodepointBoundary(summary, kMaxToolInputBytes)) + "...";
        }
        return summary;
    }

    // The text of every {type: "content"} item, or nullopt when there are
    // none (a diff- or terminal-only update leaves the output unchanged).
    std::optional<std::string> ExtractToolOutput(const Json& content) {
        if (!content.is_array()) {
            return std::nullopt;
        }
        std::optional<std::string> output;
        for (const Json& item : content) {
            if (!item.is_object() || StringField(item, "type", std::string()) != "content" || !item.contains("content") ||
                !item["content"].is_object() || StringField(item["content"], "type", std::string()) != "text") {
                continue;
            }
            if (!output) {
                output.emplace();
            }
            else {
                output->push_back('\n');
            }
            *output += StringField(item["content"], "text", std::string());
        }
        if (output && output->size() > kMaxToolOutputBytes) {
            const std::size_t total = output->size();
            *output                 = output->substr(0, text::SnapDownToCodepointBoundary(*output, kMaxToolOutputBytes)) + "\n... [" + std::to_string(total) + " bytes total]";
        }
        return output;
    }

} // namespace

Manager::Manager(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop) : bufferList_(bufferList), eventLoop_(eventLoop) {
}

Manager::~Manager() {
    // See the header's doc comment -- EndSession normally does this, but a
    // destructor that runs without one first (a session/prompt request
    // abandoned mid-flight, e.g. an owning panel torn down directly) must
    // not leak the "ACP" mode-line spinner for the rest of the process.
    if (promptInFlight_) {
        editor::EndBackgroundActivity(kAcpActivity);
    }
}

Manager::SessionState Manager::State() const {
    return state_;
}

const std::string& Manager::AgentName() const {
    return agentName_;
}

const std::vector<Manager::TranscriptEntry>& Manager::Transcript() const {
    return transcript_;
}

std::size_t Manager::TranscriptGeneration() const {
    return transcriptGeneration_;
}

void Manager::SetOnTranscriptChanged(std::function<void()> handler) {
    onTranscriptChanged_ = std::move(handler);
}

void Manager::SetMcpBridgeServer(mcp::BridgeServer* server) {
    mcpBridgeServer_ = server;
}

const std::vector<Manager::SessionMode>& Manager::Modes() const {
    return modes_;
}

const std::string& Manager::CurrentModeId() const {
    return currentModeId_;
}

const std::vector<Manager::ConfigOption>& Manager::ConfigOptions() const {
    return configOptions_;
}

const Manager::ConfigOption* Manager::ConfigOptionByCategory(std::string_view category) const {
    const auto it = std::find_if(configOptions_.begin(), configOptions_.end(),
                                 [category](const ConfigOption& option) { return option.category == category; });
    return it == configOptions_.end() ? nullptr : &*it;
}

Json Manager::McpServers() {
    Json mcpServers = Json::array();
    if (mcpBridgeServer_ && mcp::McpBridgeEnabled()) {
        try {
            mcpBridgeServer_->Start(); // idempotent -- a no-op if already listening
            mcpServers.push_back(Json{
                {"type", "stdio"},
                {"name", "ned"},
                {"command", SelfExecutablePath().string()},
                {"args", Json::array({"--mcp-stdio-relay", mcpBridgeServer_->SocketPath().string()})},
                {"env", Json::array()},
            });
        }
        catch (const std::exception& e) {
            // A failed bridge start (runtime dir/socket trouble) shouldn't
            // block the session itself -- fall back to no MCP tools, same
            // as if no bridge were wired at all.
            AppendToOutputBuffer(std::string("\n[MCP bridge unavailable: ") + e.what() + "]\n");
        }
    }
    return mcpServers;
}

const std::string& Manager::SessionId() const {
    return sessionId_;
}

bool Manager::CanResumeSessions() const {
    return agentSupportsListSessions_ && (agentSupportsLoadSession_ || agentSupportsResume_);
}

void Manager::ListSessions(std::function<void(std::vector<SessionSummary> sessions, std::string error)> done) {
    if (state_ == SessionState::Starting) {
        // Answered once the handshake settles, either way.
        whenSessionSettles_.push_back([this, done = std::move(done)]() mutable { ListSessions(std::move(done)); });
        return;
    }
    if (state_ != SessionState::Active || !client_) {
        done({}, "No active ACP session (see acp-start-session).");
        return;
    }
    if (!CanResumeSessions()) {
        done({}, "This agent can't list or resume its sessions.");
        return;
    }
    auto sessions = std::make_shared<std::vector<SessionSummary>>();
    auto fetch    = std::make_shared<std::function<void(std::optional<std::string>, int)>>();
    // Bounded: enough history to pick from without walking an agent's
    // entire archive one page at a time.
    constexpr int kMaxPages = 5;
    // Each pending response owns the fetcher; the fetcher only refers back
    // weakly, so a response the client drops (the session ended) frees it.
    *fetch = [this, sessions, weakFetch = std::weak_ptr(fetch), done = std::move(done)](std::optional<std::string> cursor, int page) {
        Json params{{"cwd", editor::ProjectRoot().string()}};
        if (cursor) {
            params["cursor"] = *cursor;
        }
        client_->SendRequest("session/list", params, [sessions, fetch = weakFetch.lock(), done, page](std::optional<Json> result, std::optional<Json> error) {
            if (error || !result || !result->is_object()) {
                const std::string message = error ? StringField(*error, "message", std::string("unknown error")) : std::string("bad response");
                done(std::move(*sessions), "session/list failed: " + message);
                return;
            }
            const Json list = result->value("sessions", Json::array());
            for (const Json& entry : list.is_array() ? list : Json::array()) {
                if (entry.is_object() && entry.contains("sessionId") && entry["sessionId"].is_string()) {
                    sessions->push_back({.sessionId = entry["sessionId"].get<std::string>(),
                                         .title     = entry.contains("title") && entry["title"].is_string() ? entry["title"].get<std::string>() : std::string(),
                                         .updatedAt = entry.contains("updatedAt") && entry["updatedAt"].is_string() ? entry["updatedAt"].get<std::string>() : std::string()});
                }
            }
            if (result->contains("nextCursor") && (*result)["nextCursor"].is_string() && page + 1 < kMaxPages) {
                (*fetch)((*result)["nextCursor"].get<std::string>(), page + 1);
                return;
            }
            done(std::move(*sessions), std::string());
        });
    };
    (*fetch)(std::nullopt, 0);
}

std::string Manager::LoadSession(const std::string& sessionId, const std::string& title) {
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session.";
    }
    if (promptInFlight_) {
        return "Can't resume while a prompt is in flight.";
    }
    if (sessionId == sessionId_) {
        return "That session is already the current one.";
    }
    if (!agentSupportsLoadSession_ && !agentSupportsResume_) {
        return "This agent can't resume sessions.";
    }
    const std::string label      = title.empty() ? sessionId : title;
    const bool        replay     = agentSupportsLoadSession_;
    const std::string previousId = sessionId_;
    // Everything the old session advertised is replaced by what the loaded
    // one answers with.
    sessionId_ = sessionId;
    availableCommands_.clear();
    sessionTitle_ = title;
    usage_.reset();
    livePlanEntryIndex_.reset();
    PushSessionEvent(std::string(replay ? "resuming: " : "resumed: ") + label);
    replaying_ = replay;
    client_->SendRequest(replay ? "session/load" : "session/resume",
                         Json{{"sessionId", sessionId}, {"cwd", editor::ProjectRoot().string()}, {"mcpServers", McpServers()}},
                         [this, sessionId, previousId, replay](std::optional<Json> result, std::optional<Json> error) {
                             replaying_ = false;
                             if (sessionId_ != sessionId) {
                                 return; // superseded by a later resume or a new session
                             }
                             if (error) {
                                 sessionId_ = previousId;
                                 PushSessionEvent("resume failed: " + StringField(*error, "message", std::string("unknown error")));
                                 return;
                             }
                             if (result) {
                                 ParseSessionSettings(*result);
                             }
                             if (!replay) {
                                 PushSessionEvent("history not replayed -- the agent only resumes");
                             }
                         });
    return "Resuming " + label + ".";
}

const std::vector<Manager::AvailableCommand>& Manager::AvailableCommands() const {
    return availableCommands_;
}

const std::string& Manager::SessionTitle() const {
    return sessionTitle_;
}

const std::optional<Manager::Usage>& Manager::SessionUsage() const {
    return usage_;
}

std::optional<std::chrono::steady_clock::time_point> Manager::PromptStartedAt() const {
    return promptStartedAt_;
}

void Manager::RunSessionSettledCallbacks() {
    std::vector<std::function<void()>> callbacks = std::move(whenSessionSettles_);
    whenSessionSettles_.clear();
    for (auto& callback : callbacks) {
        callback();
    }
}

void Manager::ParseSessionSettings(const Json& result) {
    if (!result.is_object()) {
        return;
    }
    if (result.contains("modes") && result["modes"].is_object()) {
        const Json& modes = result["modes"];
        modes_.clear();
        if (modes.contains("availableModes") && modes["availableModes"].is_array()) {
            for (const Json& mode : modes["availableModes"]) {
                if (mode.is_object() && mode.contains("id") && mode["id"].is_string()) {
                    modes_.push_back({.id          = mode["id"].get<std::string>(),
                                      .name        = StringField(mode, "name", mode["id"].get<std::string>()),
                                      .description = StringField(mode, "description", std::string())});
                }
            }
        }
        if (modes.contains("currentModeId") && modes["currentModeId"].is_string()) {
            ApplyCurrentModeId(modes["currentModeId"].get<std::string>());
        }
    }
    if (result.contains("configOptions")) {
        ApplyConfigOptions(result["configOptions"]);
    }
}

void Manager::ApplyConfigOptions(const Json& options) {
    if (!options.is_array()) {
        return;
    }
    configOptions_.clear();
    for (const Json& option : options) {
        if (!option.is_object() || !option.contains("id") || !option["id"].is_string()) {
            continue;
        }
        ConfigOption parsed{.id          = option["id"].get<std::string>(),
                            .name        = StringField(option, "name", option["id"].get<std::string>()),
                            .description = StringField(option, "description", std::string()),
                            .category    = StringField(option, "category", std::string()),
                            .type        = StringField(option, "type", std::string("select"))};
        if (option.contains("currentValue")) {
            const Json& current = option["currentValue"];
            parsed.currentValue = current.is_boolean()  ? (current.get<bool>() ? "true" : "false")
                                  : current.is_string() ? current.get<std::string>()
                                                        : current.dump();
        }
        // A select's options may be grouped ({group, name, options: [...]}),
        // flattened here since the panel lists them in one column.
        auto collect = [&parsed](const Json& list, auto& self) -> void {
            if (!list.is_array()) {
                return;
            }
            for (const Json& choice : list) {
                if (!choice.is_object()) {
                    continue;
                }
                if (choice.contains("value") && choice["value"].is_string()) {
                    parsed.choices.push_back({.value       = choice["value"].get<std::string>(),
                                              .name        = StringField(choice, "name", choice["value"].get<std::string>()),
                                              .description = StringField(choice, "description", std::string())});
                }
                else if (choice.contains("options")) {
                    self(choice["options"], self);
                }
            }
        };
        if (option.contains("options")) {
            collect(option["options"], collect);
        }
        configOptions_.push_back(std::move(parsed));
    }
    if (const ConfigOption* mode = ConfigOptionByCategory("mode")) {
        currentModeId_ = mode->currentValue;
    }
}

void Manager::ApplyCurrentModeId(std::string modeId) {
    currentModeId_ = std::move(modeId);
    for (ConfigOption& option : configOptions_) {
        if (option.category == "mode") {
            option.currentValue = currentModeId_;
        }
    }
}

std::string Manager::SetMode(const std::string& modeId) {
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session.";
    }
    if (const ConfigOption* option = ConfigOptionByCategory("mode")) {
        return SetConfigOption(option->id, modeId);
    }
    if (modes_.empty()) {
        return "This agent has no modes.";
    }
    client_->SendRequest("session/set_mode", Json{{"sessionId", sessionId_}, {"modeId", modeId}},
                         [this, modeId](std::optional<Json>, std::optional<Json> error) {
                             if (error) {
                                 PushSessionEvent("mode change failed: " + StringField(*error, "message", std::string("unknown error")));
                                 return;
                             }
                             ApplyCurrentModeId(modeId);
                         });
    return "Mode change sent.";
}

std::string Manager::SetConfigOption(const std::string& configId, const std::string& value) {
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session.";
    }
    const auto it        = std::find_if(configOptions_.begin(), configOptions_.end(), [&configId](const ConfigOption& option) { return option.id == configId; });
    const Json wireValue = it != configOptions_.end() && it->type == "boolean" ? Json(value == "true") : Json(value);
    client_->SendRequest("session/set_config_option", Json{{"sessionId", sessionId_}, {"configId", configId}, {"value", wireValue}},
                         [this](std::optional<Json> result, std::optional<Json> error) {
                             if (error) {
                                 PushSessionEvent("setting change failed: " + StringField(*error, "message", std::string("unknown error")));
                                 return;
                             }
                             if (result && result->is_object() && result->contains("configOptions")) {
                                 ApplyConfigOptions((*result)["configOptions"]);
                             }
                         });
    return "Setting change sent.";
}

std::string Manager::CycleMode() {
    std::vector<std::string> ids;
    if (const ConfigOption* option = ConfigOptionByCategory("mode")) {
        for (const ConfigChoice& choice : option->choices) {
            ids.push_back(choice.value);
        }
    }
    else {
        for (const SessionMode& mode : modes_) {
            ids.push_back(mode.id);
        }
    }
    std::erase(ids, std::string("bypassPermissions"));
    if (ids.empty()) {
        return state_ == SessionState::Active ? "This agent has no modes." : "No active ACP session.";
    }
    const auto        current = std::find(ids.begin(), ids.end(), currentModeId_);
    const std::size_t next    = current == ids.end() ? 0 : (static_cast<std::size_t>(current - ids.begin()) + 1) % ids.size();
    return SetMode(ids[next]);
}

text::Buffer& Manager::OutputBuffer(const std::string& agentName) {
    const std::string bufferName = OutputBufferName(agentName);
    text::Buffer*     buffer     = bufferList_.Find(bufferName);
    if (!buffer) {
        buffer = &bufferList_.CreateBuffer(bufferName);
        buffer->SetReadOnly(true); // must be set before the first append -- AppendWhileReadOnly's own precondition
        // acp-panel-wrapping follow-up: this buffer has no on-disk path (a
        // pure in-memory log, see the class header comment), so it always
        // resolves to FundamentalMode()/wrapLines=false unless something
        // opts it into WrapOverrides' buffer-Name()-keyed table instead --
        // see that table's own header comment for why a real path isn't
        // used here the way the VCS commit-message buffer uses one.
        editor::SetWrapForBufferName(bufferName, true);
    }
    return *buffer;
}

void Manager::AppendToOutputBuffer(std::string_view text) {
    if (agentName_.empty()) {
        return; // no session has ever started -- nothing to append to
    }
    OutputBuffer(agentName_).AppendWhileReadOnly(text);
}

void Manager::PushTranscriptEntry(TranscriptEntry entry) {
    transcript_.push_back(std::move(entry));
    ++transcriptGeneration_;
    NotifyTranscriptChanged();
}

void Manager::PushSessionEvent(std::string text) {
    PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::SessionEvent, .text = std::move(text)});
}

void Manager::NotifyTranscriptChanged() {
    if (onTranscriptChanged_) {
        onTranscriptChanged_();
    }
}

void Manager::PushOrAppendAgentText(TranscriptEntry::Kind kind, std::string_view text) {
    if (!transcript_.empty() && transcript_.back().kind == kind) {
        transcript_.back().text += text;
        ++transcriptGeneration_;
        // Debounced, not immediate -- see agentTextRepaintDebounce_'s own
        // doc comment. A brand-new entry (the branch below) still notifies
        // synchronously: that's a discrete, meaningful event (a fresh
        // thought/answer block starting), not one more token in an existing
        // stream, and should show up without a repaint delay.
        agentTextRepaintDebounce_.Arm(eventLoop_, std::chrono::milliseconds(40), [this] { NotifyTranscriptChanged(); });
        return;
    }
    PushTranscriptEntry(TranscriptEntry{.kind = kind, .text = std::string(text)});
}

void Manager::PushOrUpdateToolCall(const Json& update) {
    // A real agent's tool_call_update frequently omits title/status entirely
    // (confirmed live against Claude Code's ACP adapter -- a follow-up update
    // often carries only content/rawOutput for an already-known toolCallId).
    // Absence must mean "unchanged," not "reset to the generic fallback" --
    // ROADMAP.md's own prediction that this parsing would need widening once
    // exercised against a real agent.
    const bool        hasTitle   = !StringField(update, "title").empty() || !StringField(update, "kind").empty();
    const std::string title      = StringField(update, "title", StringField(update, "kind", std::string("tool call")));
    const bool        hasStatus  = !StringField(update, "status").empty();
    const std::string status     = StringField(update, "status", std::string());
    const std::string toolCallId = StringField(update, "toolCallId", std::string());

    // A "diff"-typed content item, when present, is the actual before/after
    // text of a file edit -- confirmed live against Claude Code's Edit tool
    // (ACP's own {type: "diff", path, oldText, newText} shape). Most tool
    // calls never carry one, and most updates for one that does only arrive
    // on the update that actually has it (earlier updates for the same call
    // have an empty "content": []) -- so, same as title/status, absence here
    // must mean "no new diff this update," not "clear the one we already have."
    const std::optional<DiffContent> diff = update.contains("content") ? ExtractDiffContent(update["content"]) : std::nullopt;
    const std::optional<std::string> output       = update.contains("content") ? ExtractToolOutput(update["content"]) : std::nullopt;
    const bool                       hasKind      = update.contains("kind") && update["kind"].is_string();
    const bool                       hasLocations = update.contains("locations");
    const bool                       hasInput     = update.contains("rawInput");
    const Json                       meta         = update.contains("_meta") && update["_meta"].is_object() ? update["_meta"] : Json::object();

    std::optional<ToolLocation> reached;
    auto                        applyDetails = [&](TranscriptEntry& entry) {
        if (hasKind) {
            entry.toolKind = update["kind"].get<std::string>();
        }
        if (hasLocations) {
            std::vector<ToolLocation> locations = ParseLocations(update["locations"]);
            if (!locations.empty() && !replaying_ &&
                (entry.locations.empty() || entry.locations.front().path != locations.front().path ||
                 entry.locations.front().line != locations.front().line)) {
                reached = locations.front();
            }
            entry.locations = std::move(locations);
        }
        if (hasInput) {
            entry.toolInput = SummarizeToolInput(update["rawInput"]);
        }
        if (output && !entry.terminal) {
            entry.toolOutput = *output;
        }
        if (meta.contains("terminal_info")) {
            entry.terminal = true;
        }
        // A delta appends; so does terminal_output, which an agent sends
        // once, whole, at the end.
        for (const char* key : {"terminal_output_delta", "terminal_output"}) {
            if (meta.contains(key) && meta[key].is_object()) {
                if (!entry.terminal) {
                    entry.terminal = true;
                    entry.toolOutput.clear();
                }
                AppendTerminalText(entry.toolOutput, entry.terminalState, StringField(meta[key], "data", std::string()));
                entry.toolOutputTrimmed = KeepTail(entry.toolOutput, kMaxToolOutputBytes) || entry.toolOutputTrimmed;
            }
        }
        if (meta.contains("terminal_exit") && meta["terminal_exit"].is_object() && meta["terminal_exit"].contains("exit_code") &&
            meta["terminal_exit"]["exit_code"].is_number_integer()) {
            entry.exitCode = meta["terminal_exit"]["exit_code"].get<int>();
            // claude-agent-acp reports 1 for any failed command; Claude
            // Code's own output starts with the real status.
            constexpr std::string_view kExitPrefix = "Exit code ";
            if (entry.exitCode == 1 && entry.toolOutput.starts_with(kExitPrefix)) {
                int         code     = 0;
                const char* from     = entry.toolOutput.data() + kExitPrefix.size();
                const auto [end, ec] = std::from_chars(from, entry.toolOutput.data() + entry.toolOutput.size(), code);
                if (ec == std::errc() && end != from) {
                    entry.exitCode = code;
                }
            }
        }
        const bool finished = entry.status == "completed" || entry.status == "failed" || entry.status == "cancelled";
        if (finished && entry.startedAt && !entry.finishedAt) {
            entry.finishedAt = std::chrono::steady_clock::now();
        }
    };

    if (!toolCallId.empty()) {
        for (auto it = transcript_.rbegin(); it != transcript_.rend(); ++it) {
            if (it->kind == TranscriptEntry::Kind::ToolCall && it->toolCallId == toolCallId) {
                if (hasTitle) {
                    it->text = title;
                }
                if (hasStatus) {
                    it->status = status;
                }
                if (diff) {
                    it->diffOldText = diff->oldText;
                    it->diffNewText = diff->newText;
                }
                applyDetails(*it);
                ++transcriptGeneration_;
                NotifyTranscriptChanged();
                if (reached && onToolLocation_) {
                    onToolLocation_(*reached);
                }
                return;
            }
        }
    }
    TranscriptEntry entry{
        .kind        = TranscriptEntry::Kind::ToolCall,
        .text        = title,
        .status      = status,
        .toolCallId  = toolCallId.empty() ? std::nullopt : std::optional<std::string>(toolCallId),
        .diffOldText = diff ? std::optional<std::string>(diff->oldText) : std::nullopt,
        .diffNewText = diff ? std::optional<std::string>(diff->newText) : std::nullopt,
    };
    if (!replaying_) {
        entry.startedAt = std::chrono::steady_clock::now();
    }
    applyDetails(entry);
    PushTranscriptEntry(std::move(entry));
    if (reached && onToolLocation_) {
        onToolLocation_(*reached);
    }
}

void Manager::PushOrReplacePlan(const Json& update) {
    std::vector<std::string> steps;
    if (update.contains("entries") && update["entries"].is_array()) {
        for (const Json& entryJson : update["entries"]) {
            std::string content;
            if (entryJson.contains("content")) {
                if (entryJson["content"].is_string()) {
                    content = entryJson["content"].get<std::string>();
                }
                else if (entryJson["content"].is_object()) {
                    content = StringField(entryJson["content"], "text", std::string());
                }
            }
            if (content.empty()) {
                content = StringField(entryJson, "description", std::string());
            }
            const std::string status = StringField(entryJson, "status", std::string());
            const char*       glyph  = status == "completed" ? "[x] " : status == "in_progress" ? "[~] "
                                                                                                : "[ ] ";
            steps.push_back(glyph + content);
        }
    }

    if (livePlanEntryIndex_ && *livePlanEntryIndex_ < transcript_.size()) {
        transcript_[*livePlanEntryIndex_].planSteps = std::move(steps);
        ++transcriptGeneration_;
        NotifyTranscriptChanged();
        return;
    }
    livePlanEntryIndex_ = transcript_.size();
    PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::Plan, .planSteps = std::move(steps)});
}

text::Buffer* Manager::StartSession(const std::string& agentName) {
    text::Buffer& buffer = OutputBuffer(agentName);

    if (state_ != SessionState::Inactive) {
        const std::string message = "An ACP session (" + agentName_ + ") is already running -- acp-stop-session first.";
        buffer.AppendWhileReadOnly("\n" + message + "\n");
        PushSessionEvent(message);
        return &buffer;
    }

    if (!buffer.Text().empty()) {
        buffer.AppendWhileReadOnly("\n--- new session ---\n");
    }

    if (!client_) {
        const auto argv = AgentCommand(agentName);
        if (!argv) {
            const std::string message = "No command configured for ACP agent \"" + agentName + "\" (see ned/set-acp-agent).";
            buffer.AppendWhileReadOnly("\n" + message + "\n");
            PushSessionEvent(message);
            return &buffer;
        }
        try {
            client_ = std::make_unique<Client>(*argv, eventLoop_);
        }
        catch (const std::exception& e) {
            client_.reset();
            const std::string message = std::string("Failed to start ACP agent: ") + e.what();
            buffer.AppendWhileReadOnly("\n" + message + "\n");
            PushSessionEvent(message);
            return &buffer;
        }
    }
    // else: a client injected via SetClientForTesting -- run the same handshake against it.

    agentName_                    = agentName;
    state_                        = SessionState::Starting;
    agentSupportsEmbeddedContext_ = false; // re-negotiated below; see PromptAttachment's doc comment
    agentSupportsImages_          = false;
    WireClient(*client_);

    client_->SendRequest(
        "initialize",
        Json{
            {"protocolVersion", 1},
            // _meta.terminal_output[_delta]: a command's output arrives as
            // raw terminal text with its exit code (claude-agent-acp's
            // extension), not a console code block.
            {"clientCapabilities",
             {{"fs", {{"readTextFile", true}, {"writeTextFile", true}}},
              {"_meta", {{"terminal_output", true}, {"terminal_output_delta", true}}}}},
        },
        [this](std::optional<Json> result, std::optional<Json> error) {
            if (error) {
                const std::string message = "ACP initialize failed: " + StringField(*error, "message", std::string("unknown error"));
                AppendToOutputBuffer("\n" + message + "\n");
                PushSessionEvent(message);
                state_ = SessionState::Inactive;
                RunSessionSettledCallbacks();
                return;
            }
            // ACP context auto-attach follow-up: whether this agent accepts
            // ContentBlock::resource on a prompt -- see PromptAttachment's
            // own doc comment. Defensive against every field being absent
            // or the wrong shape (an agent that predates this capability
            // simply omits agentCapabilities entirely).
            const Json agentCaps          = (result && result->is_object()) ? result->value("agentCapabilities", Json::object()) : Json::object();
            const Json promptCaps         = agentCaps.is_object() ? agentCaps.value("promptCapabilities", Json::object()) : Json::object();
            agentSupportsEmbeddedContext_ = BoolField(promptCaps, "embeddedContext");
            agentSupportsImages_          = BoolField(promptCaps, "image");

            const bool loadCap = BoolField(agentCaps, "loadSession");
            const Json sessionCaps =
                agentCaps.is_object() ? agentCaps.value("sessionCapabilities", Json::object()) : Json::object();
            agentSupportsLoadSession_  = loadCap;
            agentSupportsListSessions_ = sessionCaps.is_object() && sessionCaps.contains("list");
            agentSupportsResume_       = sessionCaps.is_object() && sessionCaps.contains("resume");
            const Json meta            = result && result->is_object() ? result->value("_meta", Json::object()) : Json::object();
            agentSupportsSteering_     = meta.is_object() && meta.contains("steering") && meta["steering"].is_object() &&
                                         BoolField(meta["steering"], "supported");

            const Json mcpServers = McpServers();
            client_->SendRequest(
                "session/new",
                Json{
                    {"cwd", editor::ProjectRoot().string()},
                    {"mcpServers", mcpServers},
                },
                [this](std::optional<Json> newResult, std::optional<Json> newError) {
                    if (newError || !newResult || StringField(*newResult, "sessionId").empty()) {
                        const std::string message =
                            "session/new failed" + (newError ? (": " + StringField(*newError, "message", std::string())) : std::string());
                        AppendToOutputBuffer("\n" + message + "\n");
                        PushSessionEvent(message);
                        state_ = SessionState::Inactive;
                        RunSessionSettledCallbacks();
                        return;
                    }
                    sessionId_ = StringField(*newResult, "sessionId");
                    state_     = SessionState::Active;
                    ParseSessionSettings(*newResult);
                    RunSessionSettledCallbacks();
                    AppendToOutputBuffer("\n[session ready]\n");
                    // Deliberately not also PushSessionEvent'd into the
                    // transcript (AcpPanel's chat view) -- the panel's own
                    // title bar already reads "[Active]" the instant this
                    // fires (StateLabel), so a second "session ready" line
                    // in the conversation itself was pure noise, reported
                    // live the same way the end_turn-suppression follow-up
                    // below was. The raw *acp: <agent>* protocol-log buffer
                    // above still gets it verbatim.
                });
        });

    return &buffer;
}

Json Manager::PromptBlocks(const std::string& text, const std::vector<PromptAttachment>& attachments) const {
    Json promptBlocks = Json::array({Json{{"type", "text"}, {"text", text}}});
    for (const PromptAttachment& attachment : attachments) {
        if (attachment.image) {
            if (agentSupportsImages_) {
                promptBlocks.push_back(Json{{"type", "image"}, {"mimeType", attachment.mimeType}, {"data", attachment.text}});
            }
        }
        else if (attachment.link) {
            Json link{{"type", "resource_link"}, {"uri", attachment.uri}, {"name", attachment.name}};
            if (!attachment.mimeType.empty()) {
                link["mimeType"] = attachment.mimeType;
            }
            promptBlocks.push_back(std::move(link));
        }
        else if (agentSupportsEmbeddedContext_) {
            Json resource{{"uri", attachment.uri}, {"text", attachment.text}};
            if (!attachment.mimeType.empty()) {
                resource["mimeType"] = attachment.mimeType;
            }
            promptBlocks.push_back(Json{{"type", "resource"}, {"resource", resource}});
        }
        else {
            promptBlocks[0]["text"] =
                promptBlocks[0]["text"].get<std::string>() + "\n\n--- " + attachment.name + " ---\n" + attachment.text;
        }
    }
    return promptBlocks;
}

Manager::TranscriptEntry* Manager::FindToolCall(const std::string& toolCallId) {
    if (toolCallId.empty()) {
        return nullptr;
    }
    for (auto it = transcript_.rbegin(); it != transcript_.rend(); ++it) {
        if (it->kind == TranscriptEntry::Kind::ToolCall && it->toolCallId == toolCallId) {
            return &*it;
        }
    }
    return nullptr;
}

void Manager::StopToolTimers() {
    // A tool call the agent never closed stops counting with its turn.
    const auto now = std::chrono::steady_clock::now();
    for (TranscriptEntry& entry : transcript_) {
        if (entry.kind == TranscriptEntry::Kind::ToolCall && entry.startedAt && !entry.finishedAt) {
            entry.finishedAt = now;
        }
    }
}

std::string Manager::SendPrompt(const std::string& text, const std::vector<PromptAttachment>& attachments) {
    if (state_ != SessionState::Active) {
        return "No active ACP session (see acp-start-session).";
    }
    // ACP context auto-attach follow-up: the transcript/output buffer show
    // a compact "[attached: name, ...]" marker rather than the attachment's
    // own (potentially large) content -- same reasoning a real chat UI
    // renders an attachment as a small chip, not inlined text, in its own
    // message log. The wire request below carries the real content
    // regardless of which path (resource block vs. folded-into-text) ends
    // up using it.
    std::string displayText = text;
    std::string attachedNames;
    for (const PromptAttachment& attachment : attachments) {
        if (!attachment.link) {
            attachedNames += (attachedNames.empty() ? "" : ", ") + attachment.name;
        }
    }
    if (!attachedNames.empty()) {
        displayText += "\n\n[attached: " + attachedNames + "]";
    }
    AppendToOutputBuffer("\n> " + displayText + "\n");
    PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::UserMessage, .text = displayText});
    // ACP checkpoint/rewind follow-up: opens this turn's checkpoint,
    // finalized by FinalizePendingCheckpoint once its response arrives
    // (below) or the session ends mid-turn (EndSession). A single-line,
    // length-capped preview -- the transcript's own UserMessage entry above
    // keeps the real text verbatim, this is only for the rewind picker's
    // compact list.
    {
        std::string preview = displayText;
        std::replace(preview.begin(), preview.end(), '\n', ' ');
        constexpr std::size_t kMaxPreviewLength = 60;
        if (preview.size() > kMaxPreviewLength) {
            preview.resize(kMaxPreviewLength);
            preview += "...";
        }
        pendingCheckpoint_ = Checkpoint{
            .transcriptIndex = transcript_.size() - 1,
            .promptPreview   = std::move(preview),
            .timestamp       = std::chrono::system_clock::now(),
        };
    }
    // chat-feel follow-up: the only on-screen change between hitting Enter
    // and the first agent_message_chunk used to be nothing at all -- reads
    // as "did this hang?" for however long the agent takes to say anything.
    // Reuses the same mode-line spinner registry LSP already drives.
    promptInFlight_  = true;
    promptStartedAt_ = std::chrono::steady_clock::now();
    editor::BeginBackgroundActivity(kAcpActivity);

    // ACP context auto-attach follow-up: a real ContentBlock::resource per
    // attachment when the connected agent declared support for it
    // (agentSupportsEmbeddedContext_, see StartSession); otherwise folded
    // straight into the one text block so an agent that never declared the
    // capability still receives the content, just without its own distinct
    // rendering -- see PromptAttachment/SendPrompt's own doc comments.
    const Json promptBlocks = PromptBlocks(text, attachments);

    client_->SendRequest(
        "session/prompt",
        Json{
            {"sessionId", sessionId_},
            {"prompt", promptBlocks},
        },
        [this](std::optional<Json> result, std::optional<Json> error) {
            const auto elapsed = promptStartedAt_ ? std::chrono::steady_clock::now() - *promptStartedAt_ : std::chrono::steady_clock::duration{};
            promptInFlight_    = false;
            promptStartedAt_.reset();
            editor::EndBackgroundActivity(kAcpActivity);
            FinalizePendingCheckpoint();
            StopToolTimers();
            if (error) {
                const std::string message = "error: " + StringField(*error, "message", std::string("prompt failed"));
                AppendToOutputBuffer("\n[" + message + "]\n");
                PushSessionEvent(message);
                if (onAttention_) {
                    onAttention_(Attention::TurnFinished, elapsed);
                }
                return;
            }
            const std::string stopReason = result ? StringField(*result, "stopReason", std::string("end")) : std::string("end");
            AppendToOutputBuffer("\n[" + stopReason + "]\n");
            // chat-feel follow-up (2026-08-26): an ordinary completed turn
            // ("end_turn", or "end" -- this method's own fallback for a
            // response with no stopReason at all) used to push a "--
            // end_turn --" SessionEvent into the *transcript* (AcpPanel's
            // chat view) on every single prompt, on top of the agent's own
            // reply -- reported live as feeling like a raw protocol log, not
            // a conversation (no chat UI announces "the assistant finished
            // its turn" after every message). The raw *acp: <agent>* output
            // buffer above still gets every stopReason verbatim -- that's
            // the deliberate protocol-log surface. An unusual stop reason
            // (max_tokens, refusal, cancelled, max_turn_requests, ...) still
            // surfaces in the transcript too, since that genuinely explains
            // why a reply looks truncated or missing.
            if (stopReason != "end_turn" && stopReason != "end") {
                PushSessionEvent(stopReason);
            }
            // The next queued prompt goes out once a turn ends on its own. A
            // cancelled turn holds the queue: the user interrupted, and the
            // panel hands the queued drafts back to the composer.
            if (stopReason != "cancelled" && !queuedPrompts_.empty() && state_ == SessionState::Active) {
                QueuedPrompt next = std::move(queuedPrompts_.front());
                queuedPrompts_.pop_front();
                SendPrompt(next.text, next.attachments);
            }
            else if (onAttention_ && stopReason != "cancelled") {
                onAttention_(Attention::TurnFinished, elapsed);
            }
        });
    return "Sent.";
}

void Manager::QueuePrompt(QueuedPrompt prompt) {
    queuedPrompts_.push_back(std::move(prompt));
}

const std::deque<Manager::QueuedPrompt>& Manager::QueuedPrompts() const {
    return queuedPrompts_;
}

std::optional<Manager::QueuedPrompt> Manager::TakeLastQueued() {
    if (queuedPrompts_.empty()) {
        return std::nullopt;
    }
    QueuedPrompt last = std::move(queuedPrompts_.back());
    queuedPrompts_.pop_back();
    return last;
}

std::vector<Manager::QueuedPrompt> Manager::TakeQueue() {
    std::vector<QueuedPrompt> all(std::make_move_iterator(queuedPrompts_.begin()), std::make_move_iterator(queuedPrompts_.end()));
    queuedPrompts_.clear();
    return all;
}

bool Manager::SupportsSteering() const {
    return agentSupportsSteering_;
}

bool Manager::SupportsImages() const {
    return agentSupportsImages_;
}

std::string Manager::Steer(QueuedPrompt prompt) {
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session (see acp-start-session).";
    }
    if (!promptInFlight_) {
        return SendPrompt(prompt.text, prompt.attachments);
    }
    if (!agentSupportsSteering_) {
        QueuePrompt(std::move(prompt));
        return "This agent can't take a message mid-turn; queued for when it finishes.";
    }
    const Json blocks = PromptBlocks(prompt.text, prompt.attachments);
    client_->SendRequest(
        "_session/steering",
        Json{{"sessionId", sessionId_}, {"prompt", blocks}, {"_meta", {{"steering", {{"idleBehavior", "promptRequired"}}}}}},
        [this, prompt = std::move(prompt)](std::optional<Json> result, std::optional<Json> error) mutable {
            if (error) {
                PushSessionEvent("steering failed: " + StringField(*error, "message", std::string("unknown error")) + " -- queued instead");
                QueuePrompt(std::move(prompt));
                return;
            }
            const std::string outcome = result && result->is_object() ? StringField(*result, "outcome", std::string()) : std::string();
            if (outcome == "promptRequired") {
                // The turn ended before the message could join it.
                if (promptInFlight_) {
                    QueuePrompt(std::move(prompt));
                }
                else {
                    SendPrompt(prompt.text, prompt.attachments);
                }
                return;
            }
            AppendToOutputBuffer("\n>> " + prompt.text + "\n");
            PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::UserMessage, .text = prompt.text, .status = "steered"});
        });
    return "Steering.";
}

std::string Manager::StopSession() {
    if (state_ == SessionState::Inactive) {
        return "No active ACP session.";
    }
    // Best-effort polite close; teardown below must not depend on the agent
    // answering (or even still being alive to write to) -- same reasoning
    // as Manager::StopSession.
    // async-write-queue follow-up: PrepareForGracefulShutdown must be called
    // before this SendRequest -- see Manager::StopSession's identical
    // comment.
    try {
        if (!sessionId_.empty() && client_) {
            client_->PrepareForGracefulShutdown();
            client_->SendRequest("session/close", Json{{"sessionId", sessionId_}}, [](std::optional<Json>, std::optional<Json>) {});
        }
    }
    catch (const std::exception&) {
        // EPIPE from an already-dead agent -- teardown proceeds regardless.
    }
    EndSession("ACP session stopped.");
    return "ACP session stopped.";
}

bool Manager::CancelPrompt() {
    if (state_ != SessionState::Active || !promptInFlight_ || !client_) {
        return false;
    }
    client_->SendNotification("session/cancel", Json{{"sessionId", sessionId_}});
    return true;
}

bool Manager::PromptInFlight() const {
    return promptInFlight_;
}

void Manager::RecordCheckpointFileEdit(text::Buffer& buffer, const std::filesystem::path& path, std::size_t beforeSequence) {
    if (!pendingCheckpoint_) {
        return;
    }
    // A second (or later) write to the same path within one turn extends
    // the existing record's afterSequence rather than adding a duplicate --
    // beforeSequence must stay the sequence from *before this turn's first*
    // write to it, not this write's own.
    for (CheckpointFileRecord& record : pendingCheckpoint_->fileRecords) {
        if (record.path == path) {
            record.afterSequence = buffer.CurrentUndoSequence();
            return;
        }
    }
    pendingCheckpoint_->fileRecords.push_back(CheckpointFileRecord{
        .path           = path,
        .beforeSequence = beforeSequence,
        .afterSequence  = buffer.CurrentUndoSequence(),
    });
}

void Manager::FinalizePendingCheckpoint() {
    if (pendingCheckpoint_) {
        checkpoints_.push_back(std::move(*pendingCheckpoint_));
        pendingCheckpoint_.reset();
    }
}

std::size_t Manager::CheckpointCount() const {
    return checkpoints_.size();
}

const Manager::Checkpoint& Manager::CheckpointAt(std::size_t index) const {
    return checkpoints_.at(index);
}

Manager::RewindOutcome Manager::RewindTo(std::size_t index) {
    RewindOutcome outcome;
    if (index >= checkpoints_.size()) {
        return outcome;
    }
    outcome.description  = checkpoints_[index].promptPreview;
    outcome.turnsRewound = checkpoints_.size() - index;

    // Newest-first: a file touched by more than one of the turns being
    // rewound is walked back one hop at a time, so an intermediate turn's
    // own beforeSequence/afterSequence pair still has to match up with its
    // neighbor for the chain to continue -- exactly ProjectUndoManager's own
    // divergence check, just applied repeatedly instead of once.
    for (std::size_t i = checkpoints_.size(); i-- > index;) {
        for (const CheckpointFileRecord& record : checkpoints_[i].fileRecords) {
            text::Buffer* buffer = bufferList_.FindByPath(record.path);
            if (!buffer) {
                outcome.untrackedFiles.push_back(record.path.string());
                continue;
            }
            if (buffer->CurrentUndoSequence() != record.afterSequence) {
                outcome.divergedFiles.push_back(record.path.string());
                continue;
            }
            if (buffer->TryJumpToUndoSequence(record.beforeSequence)) {
                outcome.revertedFiles.push_back(record.path.string());
            }
        }
        for (const std::filesystem::path& path : checkpoints_[i].untrackedPaths) {
            outcome.untrackedFiles.push_back(path.string());
        }
    }

    const std::size_t truncateAt = checkpoints_[index].transcriptIndex;
    if (truncateAt < transcript_.size()) {
        transcript_.erase(transcript_.begin() + static_cast<std::ptrdiff_t>(truncateAt), transcript_.end());
        ++transcriptGeneration_;
    }
    checkpoints_.erase(checkpoints_.begin() + static_cast<std::ptrdiff_t>(index), checkpoints_.end());
    livePlanEntryIndex_.reset(); // may have pointed past the new tail

    std::string summary = "rewound " + std::to_string(outcome.turnsRewound) + " turn(s) to before \"" + outcome.description +
                          "\" -- " + std::to_string(outcome.revertedFiles.size()) + " file(s) reverted";
    if (!outcome.divergedFiles.empty()) {
        summary += ", " + std::to_string(outcome.divergedFiles.size()) + " skipped (edited since)";
    }
    if (!outcome.untrackedFiles.empty()) {
        summary += ", " + std::to_string(outcome.untrackedFiles.size()) + " unaffected (not open in ned -- see backup history)";
    }
    PushSessionEvent(summary);
    return outcome;
}

void Manager::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    if (client_ && !pendingPermissionPrompt_) {
        client_->ExpireStaleRequests(maxAge);
    }
}

void Manager::WireClient(Client& client) {
    client.SetNotificationHandler("session/update", [this](const Json& params) { HandleSessionUpdate(params); });

    client.SetRequestHandler("fs/read_text_file", [this](const Json& params, RespondFn respond) {
        const std::string pathStr = StringField(params, "path", std::string());
        std::string       content;
        if (text::Buffer* buffer = bufferList_.FindByPath(pathStr)) {
            content = buffer->Text();
        }
        else {
            std::ifstream input(pathStr, std::ios::binary);
            if (!input) {
                respond(std::nullopt, Json{{"code", 1}, {"message", "cannot open file: " + pathStr}});
                return;
            }
            std::ostringstream contentStream;
            contentStream << input.rdbuf();
            content = contentStream.str();
        }
        if (params.contains("line") && params["line"].is_number_integer()) {
            content = SliceLines(content, params["line"].get<int>(), params.contains("limit") && params["limit"].is_number_integer() ? params["limit"].get<int>() : -1);
        }
        respond(Json{{"content", content}}, std::nullopt);
    });

    client.SetRequestHandler("fs/write_text_file", [this](const Json& params, RespondFn respond) {
        const std::string pathStr = StringField(params, "path", std::string());
        const std::string content = StringField(params, "content", std::string());
        if (pathStr.empty()) {
            respond(std::nullopt, Json{{"code", 1}, {"message", "missing path"}});
            return;
        }
        // ACP checkpoint/rewind follow-up: captured before the write lands,
        // so a buffer already open in ned can be jumped straight back to
        // this exact undo node later -- see RecordCheckpointFileEdit. A
        // buffer not currently open can't be undo-tracked at all; the best
        // this can do for it is preserve the prior on-disk content as a
        // Backup.h version before it's clobbered below, for manual recovery.
        text::Buffer*              buffer = bufferList_.FindByPath(pathStr);
        std::optional<std::size_t> beforeSequence;
        if (buffer && !buffer->IsLoading()) {
            beforeSequence = buffer->CurrentUndoSequence();
        }
        else if (pendingCheckpoint_) {
            editor::BackupFileBeforeSave(pathStr);
        }
        try {
            WriteFileAtomically(pathStr, content);
        }
        catch (const std::exception& e) {
            respond(std::nullopt, Json{{"code", 1}, {"message", e.what()}});
            return;
        }
        // fs-write-external-modification reuse: the file just changed on
        // disk underneath any already-open buffer for it -- exactly the
        // situation AutoRevert/AutoMerge already solve, so reuse their own
        // gating (unmodified -> Revert, modified -> three-way
        // MergeExternalChanges) for this one buffer instead of new logic.
        if (buffer && !buffer->IsLoading()) {
            try {
                if (buffer->Modified()) {
                    (void)buffer->MergeExternalChanges();
                }
                else {
                    buffer->Revert();
                }
            }
            catch (const std::exception&) {
                // Best-effort: the disk write itself already succeeded, so
                // the agent's request is answered as successful regardless.
            }
        }
        if (pendingCheckpoint_) {
            if (buffer && beforeSequence) {
                RecordCheckpointFileEdit(*buffer, pathStr, *beforeSequence);
            }
            else if (!buffer) {
                pendingCheckpoint_->untrackedPaths.emplace_back(pathStr);
            }
        }
        respond(Json::object(), std::nullopt);
    });

    client.SetRequestHandler("session/request_permission", [this](const Json& params, RespondFn respond) {
        PermissionPrompt prompt;
        prompt.description = (params.contains("toolCall") && params["toolCall"].is_object())
                                 ? StringField(params["toolCall"], "title", StringField(params["toolCall"], "kind", std::string("Permission request")))
                                 : std::string("Permission request");
        if (params.contains("options") && params["options"].is_array()) {
            for (const Json& optionJson : params["options"]) {
                prompt.options.push_back(PermissionOption{
                    .optionId = StringField(optionJson, "optionId", std::string()),
                    .name     = StringField(optionJson, "name", std::string("option")),
                    .kind     = StringField(optionJson, "kind", std::string()),
                });
            }
        }
        // diff-preview-line-diff-utility follow-up: same {type: "diff", ...}
        // content item PushOrUpdateToolCall already parses for the
        // transcript, here read straight off the pending toolCall's own
        // params instead of an already-pushed transcript entry -- a
        // permission request arrives before any tool_call_update carrying
        // this same content would.
        if (params.contains("toolCall") && params["toolCall"].is_object() && params["toolCall"].contains("content")) {
            if (const std::optional<DiffContent> diff = ExtractDiffContent(params["toolCall"]["content"])) {
                prompt.diffOldText = diff->oldText;
                prompt.diffNewText = diff->newText;
            }
        }
        if (prompt.options.empty()) {
            // Malformed/empty options -- nothing to choose from; answer
            // cancelled immediately rather than opening a UI prompt with
            // nothing in it.
            respond(Json{{"outcome", {{"outcome", "cancelled"}}}}, std::nullopt);
            return;
        }
        // A call waiting on the user isn't running yet: its timer starts
        // once it's allowed.
        permissionToolCallId_ = params.contains("toolCall") && params["toolCall"].is_object()
                                    ? StringField(params["toolCall"], "toolCallId", std::string())
                                    : std::string();
        if (TranscriptEntry* call = FindToolCall(permissionToolCallId_)) {
            call->startedAt.reset();
        }
        pendingPermissionPrompt_  = prompt;
        pendingPermissionRespond_ = std::move(respond);
        AppendToOutputBuffer("\n[permission requested: " + prompt.description + "]\n");
        PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::Permission, .text = prompt.description});
        if (onPermissionRequest_) {
            onPermissionRequest_(prompt);
        }
        if (onAttention_) {
            onAttention_(Attention::PermissionRequested,
                         promptStartedAt_ ? std::chrono::steady_clock::now() - *promptStartedAt_ : std::chrono::steady_clock::duration{});
        }
    });

    client.SetOnDisconnected([this](std::string reason) { EndSession("ACP agent disconnected: " + reason); });
}

void Manager::HandleSessionUpdate(const Json& params) {
    if (!params.contains("update") || !params["update"].is_object()) {
        return;
    }
    // Stray updates for a session this one replaced (a resume) are not ours.
    if (params.contains("sessionId") && params["sessionId"].is_string() && !sessionId_.empty() &&
        params["sessionId"].get<std::string>() != sessionId_) {
        return;
    }
    const Json&       update = params["update"];
    const std::string kind   = StringField(update, "sessionUpdate", std::string());

    if (kind == "agent_message_chunk" || kind == "agent_thought_chunk" || kind == "user_message_chunk") {
        if (update.contains("content") && update["content"].is_object() && StringField(update["content"], "type", std::string()) == "text") {
            const std::string text = StringField(update["content"], "text", std::string());
            AppendToOutputBuffer(text);
            // user_message_chunk is the agent echoing what SendPrompt
            // already pushed as one clean Kind::UserMessage entry --
            // coalescing it here too would duplicate that entry.
            // agent_thought_chunk is routed to its own Kind (AgentThought)
            // rather than folded into AgentText -- see TranscriptEntry::Kind's
            // own doc comment.
            if (kind == "user_message_chunk" && replaying_) {
                // A replayed prompt. Consecutive chunks of one message
                // coalesce; a new messageId starts the next.
                const std::string messageId = StringField(update, "messageId", std::string());
                if (!transcript_.empty() && transcript_.back().kind == TranscriptEntry::Kind::UserMessage && messageId == replayUserMessageId_) {
                    transcript_.back().text += text;
                    ++transcriptGeneration_;
                    NotifyTranscriptChanged();
                }
                else {
                    PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::UserMessage, .text = text});
                }
                replayUserMessageId_ = messageId;
            }
            else if (kind == "agent_thought_chunk") {
                PushOrAppendAgentText(TranscriptEntry::Kind::AgentThought, text);
            }
            else if (kind == "agent_message_chunk") {
                PushOrAppendAgentText(TranscriptEntry::Kind::AgentText, text);
            }
        }
        return;
    }
    if (kind == "tool_call" || kind == "tool_call_update") {
        const std::string title = StringField(update, "title", StringField(update, "kind", std::string("tool call")));
        AppendToOutputBuffer("\n[tool: " + title + "]\n");
        PushOrUpdateToolCall(update);
        return;
    }
    if (kind == "plan") {
        PushOrReplacePlan(update);
        return;
    }
    if (kind == "current_mode_update") {
        const std::string modeId = StringField(update, "currentModeId", std::string());
        if (!modeId.empty() && modeId != currentModeId_) {
            ApplyCurrentModeId(modeId);
            const auto mode = std::find_if(modes_.begin(), modes_.end(), [&modeId](const SessionMode& m) { return m.id == modeId; });
            PushSessionEvent("mode: " + (mode == modes_.end() ? modeId : mode->name));
        }
        return;
    }
    if (kind == "available_commands_update") {
        availableCommands_.clear();
        const Json commands = update.value("availableCommands", Json::array());
        for (const Json& command : commands.is_array() ? commands : Json::array()) {
            if (!command.is_object() || !command.contains("name") || !command["name"].is_string()) {
                continue;
            }
            std::string hint;
            if (command.contains("input") && command["input"].is_object() && command["input"].contains("hint") &&
                command["input"]["hint"].is_string()) {
                hint = command["input"]["hint"].get<std::string>();
            }
            availableCommands_.push_back({.name        = command["name"].get<std::string>(),
                                          .description = StringField(command, "description", std::string()),
                                          .inputHint   = std::move(hint)});
        }
        return;
    }
    if (kind == "config_option_update") {
        ApplyConfigOptions(update.value("configOptions", Json::array()));
        return;
    }
    if (kind == "session_info_update") {
        if (update.contains("title") && update["title"].is_string()) {
            sessionTitle_ = update["title"].get<std::string>();
        }
        return;
    }
    if (kind == "usage_update") {
        auto count = [&update](const char* key) {
            return update.contains(key) && update[key].is_number_unsigned() ? update[key].get<std::uint64_t>() : std::uint64_t{0};
        };
        Usage usage{.used = count("used"), .size = count("size")};
        if (update.contains("cost") && update["cost"].is_object() && update["cost"].contains("amount") &&
            update["cost"]["amount"].is_number()) {
            usage.costAmount   = update["cost"]["amount"].get<double>();
            usage.costCurrency = StringField(update["cost"], "currency", std::string("USD"));
        }
        usage_ = std::move(usage);
        return;
    }
    // Unrecognized/forward-compatible update kind -- see this class's own
    // header comment on why this isn't treated as an error.
}

void Manager::EndSession(std::string reason) {
    if (state_ == SessionState::Inactive) {
        return; // e.g. disconnect EOF arriving after an explicit StopSession already tore down
    }
    state_     = SessionState::Inactive;
    replaying_ = false;
    if (promptInFlight_) {
        // A prompt was still outstanding when the session ended out from
        // under it (StopSession mid-turn, or the agent disconnecting) --
        // its own SendRequest callback is now abandoned (Client's
        // documented "dropped, uninvoked" shutdown contract), so nothing
        // else would ever end this spinner.
        promptInFlight_ = false;
        editor::EndBackgroundActivity(kAcpActivity);
        FinalizePendingCheckpoint();
    }
    StopToolTimers();
    for (const QueuedPrompt& dropped : queuedPrompts_) {
        PushSessionEvent("not sent: " + dropped.text);
    }
    queuedPrompts_.clear();
    sessionId_.clear();
    availableCommands_.clear();
    modes_.clear();
    currentModeId_.clear();
    configOptions_.clear();
    sessionTitle_.clear();
    usage_.reset();
    promptStartedAt_.reset();
    pendingPermissionPrompt_.reset();
    pendingPermissionRespond_ = nullptr;
    livePlanEntryIndex_.reset();
    // lsp-use-after-free follow-up: client_ used to move into retired_ here
    // instead of destroying in place, deferring to the next StartSession.
    // Confirmed live elsewhere in this codebase that deferring isn't what
    // actually makes this safe (Client's own identical pattern still
    // raced a periodic tick against a background thread's own Post()ed
    // callback for the same object) -- the real fix now lives in Client
    // itself (alive_, see Client.h's header comment), so plain immediate
    // destruction is safe regardless of timing.
    client_.reset();
    AppendToOutputBuffer("\n[" + reason + "]\n");
    PushSessionEvent(reason);
    RunSessionSettledCallbacks();
    if (onSessionEnded_) {
        onSessionEnded_(std::move(reason));
    }
}

void Manager::SetOnToolLocation(std::function<void(const ToolLocation&)> handler) {
    onToolLocation_ = std::move(handler);
}

void Manager::SetOnAttention(std::function<void(Attention, std::chrono::steady_clock::duration)> handler) {
    onAttention_ = std::move(handler);
}

void Manager::SetOnPermissionRequest(std::function<void(const PermissionPrompt&)> handler) {
    onPermissionRequest_ = std::move(handler);
}

void Manager::ResolvePermissionPrompt(const std::string& optionId) {
    if (!pendingPermissionRespond_) {
        return;
    }
    // acp-panel-permission-resolution follow-up: confirmed live via ASan --
    // AcpPanel::OnEvent's own caller passes pending.options[index].optionId,
    // a reference *into* pendingPermissionPrompt_ itself (the very object
    // .reset() below destroys). Copy first so every use below reads a
    // stable, independent string instead of freed memory.
    const std::string optionIdCopy = optionId;
    RespondFn         respond      = std::move(pendingPermissionRespond_);
    for (const PermissionOption& option : pendingPermissionPrompt_->options) {
        TranscriptEntry* call = FindToolCall(permissionToolCallId_);
        if (option.optionId == optionIdCopy && option.kind.starts_with("allow") && call && !call->finishedAt && !replaying_) {
            call->startedAt = std::chrono::steady_clock::now();
        }
    }
    pendingPermissionPrompt_.reset();
    AppendToOutputBuffer("[selected: " + optionIdCopy + "]\n");
    PushSessionEvent("selected: " + optionIdCopy);
    respond(Json{{"outcome", {{"outcome", "selected"}, {"optionId", optionIdCopy}}}}, std::nullopt);
}

void Manager::CancelPermissionPrompt() {
    if (!pendingPermissionRespond_) {
        return;
    }
    RespondFn respond = std::move(pendingPermissionRespond_);
    pendingPermissionPrompt_.reset();
    AppendToOutputBuffer("[permission cancelled]\n");
    PushSessionEvent("permission cancelled");
    respond(Json{{"outcome", {{"outcome", "cancelled"}}}}, std::nullopt);
}

const std::optional<Manager::PermissionPrompt>& Manager::PendingPermissionPrompt() const {
    return pendingPermissionPrompt_;
}

void Manager::SetOnSessionEnded(std::function<void(std::string)> handler) {
    onSessionEnded_ = std::move(handler);
}

Client& Manager::SetClientForTesting(std::unique_ptr<Client> client) {
    client_ = std::move(client);
    return *client_;
}

} // namespace ned::editor::acp
