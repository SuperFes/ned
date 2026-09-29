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
#include "ContentBlocks.h"
#include "Editor/BackgroundActivity.h"
#include "Editor/Backup.h"
#include "Editor/Mcp/BridgeServer.h"
#include "Editor/Mcp/BridgeSetting.h"
#include "Editor/Project/Root.h"
#include "Editor/WrapOverrides.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/Utf8.h"
#include "TurnFiles.h"
#include "TurnReview.h"

namespace ned::editor::acp {

namespace {

    // A field that is missing, null or of another type reads as `fallback`:
    // an agent's JSON is input, and nlohmann's value() throws on a present
    // key of the wrong type (a null messageId in a replayed session, seen
    // live).
    // The property names of an elicitation request's schema, in the order
    // the raw message lists them.
    std::vector<std::string> SchemaPropertyOrder(std::string_view frame) {
        std::vector<std::string> order;
        try {
            const nlohmann::ordered_json message = nlohmann::ordered_json::parse(frame);
            const nlohmann::ordered_json properties =
                message.value("params", nlohmann::ordered_json::object()).value("requestedSchema", nlohmann::ordered_json::object()).value("properties", nlohmann::ordered_json::object());
            for (const auto& [key, value] : properties.items()) {
                order.push_back(key);
            }
        }
        catch (const std::exception&) {
            // Not an object somewhere along the way: no order to keep.
        }
        return order;
    }

    // ACP's auth_required error.
    bool IsAuthRequired(const Json& error) {
        constexpr int kAuthRequired = -32000;
        return error.is_object() && error.contains("code") && error["code"].is_number_integer() && error["code"].get<int>() == kAuthRequired;
    }

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

    // The tool call a subagent's update belongs to, when it's a subagent's.
    std::string ParentToolCallId(const Json& update) {
        const Json meta = update.is_object() ? update.value("_meta", Json::object()) : Json::object();
        return meta.is_object() && meta.contains("claudeCode") ? StringField(meta["claudeCode"], "parentToolUseId") : std::string();
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
    sessions_.push_back(std::make_unique<Session>());
    sessions_.back()->key = nextSessionKey_++;
    current_              = sessions_.back().get();
}

Manager::~Manager() {
    // See the header's doc comment -- EndSession normally does this, but a
    // destructor that runs without one first (a session/prompt request
    // abandoned mid-flight, e.g. an owning panel torn down directly) must
    // not leak the "ACP" mode-line spinner for the rest of the process.
    for (const auto& session : sessions_) {
        if (session->promptInFlight) {
            editor::EndBackgroundActivity(kAcpActivity);
        }
    }
}

Manager::SessionState Manager::State() const {
    return state_;
}

const std::string& Manager::AgentName() const {
    return agentName_;
}

const std::vector<Manager::TranscriptEntry>& Manager::Transcript() const {
    const Session& s = *current_;
    return s.transcript;
}

std::size_t Manager::TranscriptGeneration() const {
    return current_->generation;
}

void Manager::SetOnTranscriptChanged(std::function<void()> handler) {
    onTranscriptChanged_ = std::move(handler);
}

void Manager::SetMcpBridgeServer(mcp::BridgeServer* server) {
    mcpBridgeServer_ = server;
}

const std::vector<Manager::SessionMode>& Manager::Modes() const {
    const Session& s = *current_;
    return s.modes;
}

const std::string& Manager::CurrentModeId() const {
    const Session& s = *current_;
    return s.currentModeId;
}

const std::vector<Manager::ConfigOption>& Manager::ConfigOptions() const {
    const Session& s = *current_;
    return s.configOptions;
}

const Manager::ConfigOption* Manager::ConfigOptionByCategory(std::string_view category) const {
    const Session& s  = *current_;
    const auto     it = std::find_if(s.configOptions.begin(), s.configOptions.end(),
                                     [category](const ConfigOption& option) { return option.category == category; });
    return it == s.configOptions.end() ? nullptr : &*it;
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
    const Session& s = *current_;
    return s.id;
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
        listSessionsRequest_ = client_->SendRequest("session/list", params, [this, sessions, fetch = weakFetch.lock(), done, page](std::optional<Json> result, std::optional<Json> error) {
            listSessionsRequest_.reset();
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

void Manager::CancelListSessions() {
    if (listSessionsRequest_ && client_) {
        client_->CancelRequest(*listSessionsRequest_);
    }
    listSessionsRequest_.reset();
}

bool Manager::CanDeleteSessions() const {
    return agentSupportsDelete_;
}

void Manager::DeleteSession(const std::string& sessionId, std::function<void(std::string error)> done) {
    if (state_ != SessionState::Active || !client_) {
        done("No active ACP session.");
        return;
    }
    if (!agentSupportsDelete_) {
        done("This agent can't delete sessions.");
        return;
    }
    if (sessionId == current_->id) {
        done("That's the session you're in.");
        return;
    }
    if (std::any_of(sessions_.begin(), sessions_.end(), [&sessionId](const auto& open) { return open->id == sessionId; })) {
        done("That session is open in another tab.");
        return;
    }
    client_->SendRequest("session/delete", Json{{"sessionId", sessionId}}, [done = std::move(done)](std::optional<Json>, std::optional<Json> error) {
        done(error ? "session/delete failed: " + StringField(*error, "message", std::string("unknown error")) : std::string());
    });
}

Manager::Session* Manager::SessionFor(const Json& params) {
    const std::string id = StringField(params, "sessionId");
    if (id.empty()) {
        return current_;
    }
    for (const auto& session : sessions_) {
        if (session->id == id) {
            return session.get();
        }
    }
    // An agent may speak about a session before session/new answers with its id.
    return current_->id.empty() ? current_ : nullptr;
}

Manager::Session* Manager::SessionByKey(std::uint64_t key) {
    for (const auto& session : sessions_) {
        if (session->key == key) {
            return session.get();
        }
    }
    return nullptr;
}

void Manager::Bump(Session& session) {
    session.generation = ++generationStamp_;
}

void Manager::NoteAttention(Session& session, Attention attention, std::chrono::steady_clock::duration turnElapsed) {
    if (&session != current_) {
        session.attention = true;
        NotifyTranscriptChanged();
    }
    if (onAttention_) {
        onAttention_(attention, turnElapsed);
    }
}

std::vector<Manager::SessionTab> Manager::SessionTabs() const {
    std::vector<SessionTab> tabs;
    for (const auto& session : sessions_) {
        std::string label = session->title;
        if (label.empty()) {
            const auto prompt = std::find_if(session->transcript.begin(), session->transcript.end(),
                                             [](const TranscriptEntry& entry) { return entry.kind == TranscriptEntry::Kind::UserMessage; });
            label             = prompt == session->transcript.end() ? std::string() : prompt->text.substr(0, prompt->text.find('\n'));
        }
        tabs.push_back({.key       = session->key,
                        .label     = std::move(label),
                        .current   = session.get() == current_,
                        .live      = !session->id.empty() || session->creating,
                        .busy      = session->promptInFlight,
                        .attention = session->attention});
    }
    return tabs;
}

std::uint64_t Manager::CurrentSessionKey() const {
    return current_->key;
}

void Manager::SelectSession(std::uint64_t key) {
    Session* session = SessionByKey(key);
    if (!session || session == current_) {
        return;
    }
    current_            = session;
    current_->attention = false;
    Bump(*current_);
    NotifyTranscriptChanged();
}

void Manager::CycleSession(int direction) {
    const auto it    = std::find_if(sessions_.begin(), sessions_.end(), [this](const auto& session) { return session.get() == current_; });
    const auto count = static_cast<std::ptrdiff_t>(sessions_.size());
    const auto index = ((it - sessions_.begin()) + direction % count + count) % count;
    SelectSession(sessions_[static_cast<std::size_t>(index)]->key);
}

std::string Manager::CloseSession(std::uint64_t key) {
    const auto it = std::find_if(sessions_.begin(), sessions_.end(), [key](const auto& session) { return session->key == key; });
    if (it == sessions_.end()) {
        return "No such conversation.";
    }
    if (sessions_.size() == 1) {
        return "That's the only conversation; acp-stop-session ends it.";
    }
    Session& s = **it;
    if (client_ && !s.id.empty()) {
        if (s.promptInFlight) {
            client_->SendNotification("session/cancel", Json{{"sessionId", s.id}});
        }
        if (s.pendingPermissionRespond) {
            s.pendingPermissionRespond(Json{{"outcome", {{"outcome", "cancelled"}}}}, std::nullopt);
        }
        if (s.pendingElicitationRespond) {
            s.pendingElicitationRespond(Json{{"action", "cancel"}}, std::nullopt);
        }
        if (agentSupportsClose_) {
            client_->SendRequest("session/close", Json{{"sessionId", s.id}}, [](std::optional<Json>, std::optional<Json>) {});
        }
    }
    if (s.promptInFlight) {
        editor::EndBackgroundActivity(kAcpActivity);
    }
    const bool        wasCurrent = &s == current_;
    const std::size_t index      = static_cast<std::size_t>(it - sessions_.begin());
    sessions_.erase(it);
    if (wasCurrent) {
        current_ = sessions_[index == 0 ? 0 : index - 1].get();
        Bump(*current_);
    }
    NotifyTranscriptChanged();
    return "Closed the conversation.";
}

std::string Manager::NewSession() {
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session (see acp-start-session).";
    }
    sessions_.push_back(std::make_unique<Session>());
    Session& s = *sessions_.back();
    s.key      = nextSessionKey_++;
    s.creating = true;
    SelectSession(s.key);
    client_->SendRequest("session/new", Json{{"cwd", editor::ProjectRoot().string()}, {"mcpServers", McpServers()}},
                         [this, key = s.key](std::optional<Json> result, std::optional<Json> error) {
                             Session* found = SessionByKey(key);
                             if (!found) {
                                 CloseOrphan(result);
                                 return;
                             }
                             Session& s = *found;
                             s.creating = false;
                             if (error && IsAuthRequired(*error)) {
                                 RequireLogin();
                                 return;
                             }
                             if (error || !result || StringField(*result, "sessionId").empty()) {
                                 PushSessionEvent(s, "session/new failed" + (error ? ": " + StringField(*error, "message", std::string()) : std::string()));
                                 return;
                             }
                             s.id = StringField(*result, "sessionId");
                             ParseSessionSettings(s, *result);
                             Bump(s);
                             NotifyTranscriptChanged();
                         });
    return "Starting a new conversation.";
}

void Manager::CloseOrphan(const std::optional<Json>& result) {
    // The tab closed before the agent answered with its session.
    const std::string id = result ? StringField(*result, "sessionId") : std::string();
    if (!id.empty() && agentSupportsClose_ && client_) {
        client_->SendRequest("session/close", Json{{"sessionId", id}}, [](std::optional<Json>, std::optional<Json>) {});
    }
}

bool Manager::CanForkSessions() const {
    return agentSupportsFork_ && state_ == SessionState::Active;
}

std::vector<Manager::ForkPoint> Manager::ForkPoints() const {
    std::vector<ForkPoint> points;
    if (!agentSupportsFork_ || !agentForksAtMessage_) {
        return points;
    }
    const std::vector<TranscriptEntry>& transcript = current_->transcript;
    // Turns start at each prompt the user sent, a steered message included in
    // the turn it joined.
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < transcript.size(); ++i) {
        if (transcript[i].kind == TranscriptEntry::Kind::UserMessage && transcript[i].status != "steered") {
            starts.push_back(i);
        }
    }
    for (std::size_t turn = 0; turn + 1 < starts.size(); ++turn) {
        const std::size_t end = starts[turn + 1];
        for (std::size_t i = end; i-- > starts[turn];) {
            if (transcript[i].kind == TranscriptEntry::Kind::AgentText && !transcript[i].itemId.empty() && transcript[i].parentToolCallId.empty()) {
                const std::string& prompt = transcript[starts[turn]].text;
                points.push_back({.transcriptEnd = end, .preview = prompt.substr(0, prompt.find('\n')), .messageId = transcript[i].itemId});
                break;
            }
        }
    }
    std::reverse(points.begin(), points.end());
    return points;
}

std::string Manager::ForkSession(const std::optional<ForkPoint>& point) {
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session (see acp-start-session).";
    }
    if (!agentSupportsFork_) {
        return "This agent can't fork a session.";
    }
    Session& source = *current_;
    if (source.id.empty() || source.creating) {
        return "This conversation isn't on the agent to fork.";
    }
    if (source.promptInFlight) {
        return "Can't fork mid-turn; wait for it or interrupt it.";
    }
    sessions_.push_back(std::make_unique<Session>());
    Session& fork   = *sessions_.back();
    fork.key        = nextSessionKey_++;
    fork.creating   = true;
    fork.title      = source.title;
    fork.transcript = source.transcript;
    if (point && point->transcriptEnd < fork.transcript.size()) {
        fork.transcript.resize(point->transcriptEnd);
    }
    fork.modes             = source.modes;
    fork.currentModeId     = source.currentModeId;
    fork.configOptions     = source.configOptions;
    fork.availableCommands = source.availableCommands;
    PushSessionEvent(fork, point ? "forked after \"" + point->preview + "\"" : std::string("forked"));
    SelectSession(fork.key);
    Json params{{"sessionId", source.id}, {"cwd", editor::ProjectRoot().string()}, {"mcpServers", McpServers()}};
    if (point) {
        params["_meta"] = {{"jetbrains", {{"air", {{"fork", {{"version", 1}, {"messageId", point->messageId}}}}}}}};
    }
    client_->SendRequest("session/fork", params,
                         [this, key = fork.key, sourceKey = source.key](std::optional<Json> result, std::optional<Json> error) {
                             Session* found = SessionByKey(key);
                             if (!found) {
                                 CloseOrphan(result);
                                 return;
                             }
                             Session& s = *found;
                             s.creating = false;
                             if (error || !result || StringField(*result, "sessionId").empty()) {
                                 const std::string message =
                                     "fork failed" + (error ? ": " + StringField(*error, "message", std::string("unknown error")) : std::string());
                                 if (Session* origin = SessionByKey(sourceKey)) {
                                     SelectSession(origin->key);
                                     CloseSession(key);
                                     PushSessionEvent(*origin, message);
                                 }
                                 else {
                                     PushSessionEvent(s, message);
                                 }
                                 return;
                             }
                             s.id = StringField(*result, "sessionId");
                             ParseSessionSettings(s, *result);
                             Bump(s);
                             NotifyTranscriptChanged();
                         });
    return "Forking the conversation.";
}

std::string Manager::LoadSession(const std::string& sessionId, const std::string& title) {
    Session& s = *current_;
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session.";
    }
    if (s.promptInFlight) {
        return "Can't resume while a prompt is in flight.";
    }
    if (sessionId == s.id) {
        return "That session is already the current one.";
    }
    for (const auto& open : sessions_) {
        if (open->id == sessionId) {
            SelectSession(open->key);
            return "Switched to " + (title.empty() ? sessionId : title) + ".";
        }
    }
    if (!agentSupportsLoadSession_ && !agentSupportsResume_) {
        return "This agent can't resume sessions.";
    }
    const std::string label      = title.empty() ? sessionId : title;
    const bool        replay     = agentSupportsLoadSession_;
    const std::string previousId = s.id;
    // Everything the old session advertised is replaced by what the loaded
    // one answers with.
    s.id = sessionId;
    s.availableCommands.clear();
    s.title = title;
    s.usage.reset();
    s.livePlanEntryIndex.reset();
    PushSessionEvent(s, std::string(replay ? "resuming: " : "resumed: ") + label);
    s.replaying = replay;
    client_->SendRequest(replay ? "session/load" : "session/resume",
                         Json{{"sessionId", sessionId}, {"cwd", editor::ProjectRoot().string()}, {"mcpServers", McpServers()}},
                         [this, key = s.key, sessionId, previousId, replay](std::optional<Json> result, std::optional<Json> error) {
                             Session* found = SessionByKey(key);
                             if (!found) {
                                 return;
                             }
                             Session& s  = *found;
                             s.replaying = false;
                             if (s.id != sessionId) {
                                 return; // superseded by a later resume or a new session
                             }
                             if (error) {
                                 s.id = previousId;
                                 if (IsAuthRequired(*error)) {
                                     RequireLogin();
                                     return;
                                 }
                                 PushSessionEvent(s, "resume failed: " + StringField(*error, "message", std::string("unknown error")));
                                 return;
                             }
                             if (result) {
                                 ParseSessionSettings(s, *result);
                             }
                             // The agent keeps a session it's not on alive until told.
                             if (agentSupportsClose_ && !previousId.empty() && client_) {
                                 client_->SendRequest("session/close", Json{{"sessionId", previousId}}, [](std::optional<Json>, std::optional<Json>) {});
                             }
                             if (!replay) {
                                 PushSessionEvent(s, "history not replayed -- the agent only resumes");
                             }
                         });
    return "Resuming " + label + ".";
}

const std::vector<Manager::AvailableCommand>& Manager::AvailableCommands() const {
    const Session& s = *current_;
    return s.availableCommands;
}

const std::string& Manager::SessionTitle() const {
    const Session& s = *current_;
    return s.title;
}

const std::optional<Manager::Usage>& Manager::SessionUsage() const {
    const Session& s = *current_;
    return s.usage;
}

std::optional<std::chrono::steady_clock::time_point> Manager::PromptStartedAt() const {
    const Session& s = *current_;
    return s.promptStartedAt;
}

void Manager::RunSessionSettledCallbacks() {
    std::vector<std::function<void()>> callbacks = std::move(whenSessionSettles_);
    whenSessionSettles_.clear();
    for (auto& callback : callbacks) {
        callback();
    }
}

void Manager::ParseSessionSettings(Session& s, const Json& result) {
    if (!result.is_object()) {
        return;
    }
    if (result.contains("modes") && result["modes"].is_object()) {
        const Json& modes = result["modes"];
        s.modes.clear();
        if (modes.contains("availableModes") && modes["availableModes"].is_array()) {
            for (const Json& mode : modes["availableModes"]) {
                if (mode.is_object() && mode.contains("id") && mode["id"].is_string()) {
                    s.modes.push_back({.id          = mode["id"].get<std::string>(),
                                       .name        = StringField(mode, "name", mode["id"].get<std::string>()),
                                       .description = StringField(mode, "description", std::string())});
                }
            }
        }
        if (modes.contains("currentModeId") && modes["currentModeId"].is_string()) {
            ApplyCurrentModeId(s, modes["currentModeId"].get<std::string>());
        }
    }
    if (result.contains("configOptions")) {
        ApplyConfigOptions(s, result["configOptions"]);
    }
}

void Manager::ApplyConfigOptions(Session& s, const Json& options) {
    if (!options.is_array()) {
        return;
    }
    s.configOptions.clear();
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
        s.configOptions.push_back(std::move(parsed));
    }
    if (const ConfigOption* mode = ConfigOptionByCategory("mode")) {
        s.currentModeId = mode->currentValue;
    }
}

void Manager::ApplyCurrentModeId(Session& s, std::string modeId) {
    s.currentModeId = std::move(modeId);
    for (ConfigOption& option : s.configOptions) {
        if (option.category == "mode") {
            option.currentValue = s.currentModeId;
        }
    }
}

std::string Manager::SetMode(const std::string& modeId) {
    Session& s = *current_;
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session.";
    }
    if (const ConfigOption* option = ConfigOptionByCategory("mode")) {
        return SetConfigOption(option->id, modeId);
    }
    if (s.modes.empty()) {
        return "This agent has no modes.";
    }
    client_->SendRequest("session/set_mode", Json{{"sessionId", s.id}, {"modeId", modeId}},
                         [this, key = s.key, modeId](std::optional<Json>, std::optional<Json> error) {
                             Session* found = SessionByKey(key);
                             if (!found) {
                                 return;
                             }
                             Session& s = *found;
                             if (error) {
                                 PushSessionEvent(s, "mode change failed: " + StringField(*error, "message", std::string("unknown error")));
                                 return;
                             }
                             ApplyCurrentModeId(s, modeId);
                         });
    return "Mode change sent.";
}

std::string Manager::SetConfigOption(const std::string& configId, const std::string& value) {
    Session& s = *current_;
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session.";
    }
    const auto it        = std::find_if(s.configOptions.begin(), s.configOptions.end(), [&configId](const ConfigOption& option) { return option.id == configId; });
    const Json wireValue = it != s.configOptions.end() && it->type == "boolean" ? Json(value == "true") : Json(value);
    client_->SendRequest("session/set_config_option", Json{{"sessionId", s.id}, {"configId", configId}, {"value", wireValue}},
                         [this, key = s.key](std::optional<Json> result, std::optional<Json> error) {
                             Session* found = SessionByKey(key);
                             if (!found) {
                                 return;
                             }
                             Session& s = *found;
                             if (error) {
                                 PushSessionEvent(s, "setting change failed: " + StringField(*error, "message", std::string("unknown error")));
                                 return;
                             }
                             if (result && result->is_object() && result->contains("configOptions")) {
                                 ApplyConfigOptions(s, (*result)["configOptions"]);
                             }
                         });
    return "Setting change sent.";
}

std::string Manager::CycleMode() {
    Session&                 s = *current_;
    std::vector<std::string> ids;
    if (const ConfigOption* option = ConfigOptionByCategory("mode")) {
        for (const ConfigChoice& choice : option->choices) {
            ids.push_back(choice.value);
        }
    }
    else {
        for (const SessionMode& mode : s.modes) {
            ids.push_back(mode.id);
        }
    }
    std::erase(ids, std::string("bypassPermissions"));
    if (ids.empty()) {
        return state_ == SessionState::Active ? "This agent has no modes." : "No active ACP session.";
    }
    const auto        current = std::find(ids.begin(), ids.end(), s.currentModeId);
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

void Manager::PushTranscriptEntry(Session& s, TranscriptEntry entry) {
    s.transcript.push_back(std::move(entry));
    Bump(s);
    NotifyTranscriptChanged();
}

void Manager::PushSessionEvent(Session& s, std::string text) {
    PushTranscriptEntry(s, TranscriptEntry{.kind = TranscriptEntry::Kind::SessionEvent, .text = std::move(text)});
}

void Manager::NotifyTranscriptChanged() {
    if (onTranscriptChanged_) {
        onTranscriptChanged_();
    }
}

void Manager::PushOrAppendAgentText(Session& s, TranscriptEntry::Kind kind, std::string_view text, const std::string& messageId,
                                    const std::string& parentToolCallId) {
    // A subagent's text and the agent's own stream side by side; each
    // continues only its own.
    if (!s.transcript.empty() && s.transcript.back().kind == kind && s.transcript.back().parentToolCallId == parentToolCallId) {
        s.transcript.back().text += text;
        if (!messageId.empty()) {
            s.transcript.back().itemId = messageId;
        }
        Bump(s);
        // Debounced, not immediate -- see agentTextRepaintDebounce_'s own
        // doc comment. A brand-new entry (the branch below) still notifies
        // synchronously: that's a discrete, meaningful event (a fresh
        // thought/answer block starting), not one more token in an existing
        // stream, and should show up without a repaint delay.
        agentTextRepaintDebounce_.Arm(eventLoop_, std::chrono::milliseconds(40), [this] { NotifyTranscriptChanged(); });
        return;
    }
    TranscriptEntry entry{.kind = kind, .text = std::string(text), .itemId = messageId};
    entry.parentToolCallId = parentToolCallId;
    PushTranscriptEntry(s, std::move(entry));
}

void Manager::PushOrUpdateToolCall(Session& s, const Json& update) {
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
    const std::string                parent       = ParentToolCallId(update);

    std::optional<ToolLocation> reached;
    auto                        applyDetails = [&](TranscriptEntry& entry) {
        if (!parent.empty()) {
            entry.parentToolCallId = parent;
        }
        if (hasKind) {
            entry.toolKind = update["kind"].get<std::string>();
        }
        if (hasLocations) {
            std::vector<ToolLocation> locations = ParseLocations(update["locations"]);
            if (!locations.empty() && !s.replaying &&
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
        for (auto it = s.transcript.rbegin(); it != s.transcript.rend(); ++it) {
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
                SnapshotToolCallFiles(s, *it);
                Bump(s);
                NotifyTranscriptChanged();
                if (reached && onToolLocation_ && &s == current_) {
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
    if (!s.replaying) {
        entry.startedAt = std::chrono::steady_clock::now();
    }
    applyDetails(entry);
    SnapshotToolCallFiles(s, entry);
    PushTranscriptEntry(s, std::move(entry));
    if (reached && onToolLocation_ && &s == current_) {
        onToolLocation_(*reached);
    }
}

void Manager::PushOrReplacePlan(Session& s, const Json& update) {
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

    if (s.livePlanEntryIndex && *s.livePlanEntryIndex < s.transcript.size()) {
        s.transcript[*s.livePlanEntryIndex].planSteps = std::move(steps);
        Bump(s);
        NotifyTranscriptChanged();
        return;
    }
    s.livePlanEntryIndex = s.transcript.size();
    PushTranscriptEntry(s, TranscriptEntry{.kind = TranscriptEntry::Kind::Plan, .planSteps = std::move(steps)});
}

text::Buffer* Manager::StartSession(const std::string& agentName) {
    text::Buffer& buffer = OutputBuffer(agentName);

    if (state_ != SessionState::Inactive) {
        const std::string message = "An ACP session (" + agentName_ + ") is already running -- acp-stop-session first.";
        buffer.AppendWhileReadOnly("\n" + message + "\n");
        PushSessionEvent(*current_, message);
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
            PushSessionEvent(*current_, message);
            return &buffer;
        }
        try {
            client_ = std::make_unique<Client>(*argv, eventLoop_);
        }
        catch (const std::exception& e) {
            client_.reset();
            const std::string message = std::string("Failed to start ACP agent: ") + e.what();
            buffer.AppendWhileReadOnly("\n" + message + "\n");
            PushSessionEvent(*current_, message);
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
            // extension), not a console code block. _meta.terminal-auth:
            // agents predating auth.terminal (opencode) offer their login
            // command only when asked this way.
            {"clientCapabilities",
             {{"fs", {{"readTextFile", true}, {"writeTextFile", true}}},
              {"auth", {{"terminal", true}}},
              {"elicitation", {{"form", Json::object()}, {"url", Json::object()}}},
              {"session", {{"notices", Json::object()}, {"compaction", Json::object()}, {"configOptions", {{"boolean", Json::object()}}}}},
              {"_meta", {{"terminal_output", true}, {"terminal_output_delta", true}, {"terminal-auth", true}}}}},
        },
        [this](std::optional<Json> result, std::optional<Json> error) {
            if (error) {
                const std::string message = "ACP initialize failed: " + StringField(*error, "message", std::string("unknown error"));
                AppendToOutputBuffer("\n" + message + "\n");
                PushSessionEvent(*current_, message);
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
            agentSupportsDelete_       = sessionCaps.is_object() && sessionCaps.contains("delete");
            agentSupportsClose_        = sessionCaps.is_object() && sessionCaps.contains("close");
            agentSupportsFork_         = sessionCaps.is_object() && sessionCaps.contains("fork");
            const Json agentInfo       = result && result->is_object() ? result->value("agentInfo", Json::object()) : Json::object();
            agentForksAtMessage_       = StringField(agentInfo, "name") == "@agentclientprotocol/claude-agent-acp";
            const Json meta            = result && result->is_object() ? result->value("_meta", Json::object()) : Json::object();
            agentSupportsSteering_     = meta.is_object() && meta.contains("steering") && meta["steering"].is_object() &&
                                         BoolField(meta["steering"], "supported");

            const Json authCaps  = agentCaps.is_object() ? agentCaps.value("auth", Json::object()) : Json::object();
            agentSupportsLogout_ = authCaps.is_object() && authCaps.contains("logout") && authCaps["logout"].is_object();
            authMethods_.clear();
            const Json methods = result && result->is_object() ? result->value("authMethods", Json::array()) : Json::array();
            for (const Json& method : methods.is_array() ? methods : Json::array()) {
                const std::string id = StringField(method, "id", std::string());
                if (id.empty()) {
                    continue;
                }
                AuthMethod parsed{.id          = id,
                                  .name        = StringField(method, "name", id),
                                  .description = StringField(method, "description", std::string()),
                                  .type        = StringField(method, "type", std::string("agent"))};
                for (const Json& arg : method.contains("args") && method["args"].is_array() ? method["args"] : Json::array()) {
                    if (arg.is_string()) {
                        parsed.args.push_back(arg.get<std::string>());
                    }
                }
                auto readEnv = [&parsed](const Json& env) {
                    if (env.is_object()) {
                        for (const auto& [name, value] : env.items()) {
                            if (value.is_string()) {
                                parsed.env.emplace_back(name, value.get<std::string>());
                            }
                        }
                    }
                };
                readEnv(method.value("env", Json::object()));
                const Json legacy = method.contains("_meta") && method["_meta"].is_object() ? method["_meta"].value("terminal-auth", Json())
                                                                                            : Json();
                if (parsed.type != "terminal" && legacy.is_object() && !StringField(legacy, "command").empty()) {
                    parsed.type    = "terminal";
                    parsed.command = {StringField(legacy, "command")};
                    for (const Json& arg : legacy.value("args", Json::array())) {
                        if (arg.is_string()) {
                            parsed.command.push_back(arg.get<std::string>());
                        }
                    }
                    readEnv(legacy.value("env", Json::object()));
                }
                authMethods_.push_back(std::move(parsed));
            }

            CreateSession();
        });

    return &buffer;
}

void Manager::CreateSession() {
    client_->SendRequest(
        "session/new",
        Json{
            {"cwd", editor::ProjectRoot().string()},
            {"mcpServers", McpServers()},
        },
        [this, key = current_->key](std::optional<Json> newResult, std::optional<Json> newError) {
            // The session being created; the current one if its tab is gone.
            Session* found = SessionByKey(key);
            Session& s     = found ? *found : *current_;
            if (newError && IsAuthRequired(*newError)) {
                // The connection stays up, waiting on a login.
                sessionAwaitsLogin_ = true;
                RequireLogin();
                RunSessionSettledCallbacks();
                return;
            }
            if (newError || !newResult || StringField(*newResult, "sessionId").empty()) {
                const std::string message =
                    "session/new failed" + (newError ? (": " + StringField(*newError, "message", std::string())) : std::string());
                AppendToOutputBuffer("\n" + message + "\n");
                PushSessionEvent(s, message);
                state_ = SessionState::Inactive;
                RunSessionSettledCallbacks();
                return;
            }
            s.id       = StringField(*newResult, "sessionId");
            state_     = SessionState::Active;
            ParseSessionSettings(s, *newResult);
            RunSessionSettledCallbacks();
            // Only in the protocol log: the panel's title already says Active.
            AppendToOutputBuffer("\n[session ready]\n");
        });
}

void Manager::RequireLogin() {
    loginRequired_ = true;
    AppendToOutputBuffer("\n[login required]\n");
    PushSessionEvent(*current_, authMethods_.empty() ? "login required -- the agent offers no way to log in from here" : "login required");
    if (onLoginRequired_) {
        onLoginRequired_();
    }
}

const std::vector<Manager::AuthMethod>& Manager::AuthMethods() const {
    return authMethods_;
}

bool Manager::LoginRequired() const {
    return loginRequired_;
}

std::optional<std::vector<std::string>> Manager::LoginCommand(const AuthMethod& method) const {
    if (method.type != "terminal") {
        return std::nullopt;
    }
    if (!method.command.empty()) {
        return method.command;
    }
    std::optional<std::vector<std::string>> argv = AgentCommand(agentName_);
    if (argv) {
        argv->insert(argv->end(), method.args.begin(), method.args.end());
    }
    return argv;
}

std::string Manager::Authenticate(const std::string& methodId) {
    if (state_ == SessionState::Inactive || !client_) {
        return "No ACP session.";
    }
    client_->SendRequest("authenticate", Json{{"methodId", methodId}}, [this](std::optional<Json>, std::optional<Json> error) {
        if (error) {
            PushSessionEvent(*current_, "login failed: " + StringField(*error, "message", std::string("unknown error")));
            return;
        }
        FinishLogin(true);
    });
    return "Logging in…";
}

void Manager::FinishLogin(bool succeeded) {
    if (state_ == SessionState::Inactive || !client_) {
        return;
    }
    if (!succeeded) {
        PushSessionEvent(*current_, "login didn't finish");
        return;
    }
    loginRequired_ = false;
    PushSessionEvent(*current_, "logged in");
    if (sessionAwaitsLogin_ && state_ == SessionState::Starting) {
        sessionAwaitsLogin_ = false;
        CreateSession();
    }
}

bool Manager::CanLogout() const {
    return agentSupportsLogout_ && state_ != SessionState::Inactive;
}

std::string Manager::Logout() {
    if (state_ == SessionState::Inactive || !client_) {
        return "No ACP session.";
    }
    if (!agentSupportsLogout_) {
        return "This agent can't log out.";
    }
    client_->SendRequest("logout", Json::object(), [this](std::optional<Json>, std::optional<Json> error) {
        PushSessionEvent(*current_, error ? "logout failed: " + StringField(*error, "message", std::string("unknown error")) : std::string("logged out"));
    });
    return "Logging out…";
}

void Manager::SetOnLoginRequired(std::function<void()> handler) {
    onLoginRequired_ = std::move(handler);
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

Manager::TranscriptEntry* Manager::FindToolCall(Session& s, const std::string& toolCallId) {
    if (toolCallId.empty()) {
        return nullptr;
    }
    for (auto it = s.transcript.rbegin(); it != s.transcript.rend(); ++it) {
        if (it->kind == TranscriptEntry::Kind::ToolCall && it->toolCallId == toolCallId) {
            return &*it;
        }
    }
    return nullptr;
}

void Manager::StopToolTimers(Session& s) {
    // A tool call the agent never closed stops counting with its turn.
    const auto now = std::chrono::steady_clock::now();
    for (TranscriptEntry& entry : s.transcript) {
        if (entry.kind == TranscriptEntry::Kind::ToolCall && entry.startedAt && !entry.finishedAt) {
            entry.finishedAt = now;
        }
    }
}

std::string Manager::SendPrompt(const std::string& text, const std::vector<PromptAttachment>& attachments) {
    return SendPrompt(*current_, text, attachments);
}

std::string Manager::SendPrompt(Session& s, const std::string& text, const std::vector<PromptAttachment>& attachments) {
    if (state_ != SessionState::Active) {
        return "No active ACP session (see acp-start-session).";
    }
    if (s.creating) {
        return "This conversation is still starting.";
    }
    if (s.id.empty()) {
        return "This conversation has ended; start a new one.";
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
    TranscriptEntry message{.kind = TranscriptEntry::Kind::UserMessage, .text = displayText};
    for (const PromptAttachment& attachment : attachments) {
        if (attachment.image && agentSupportsImages_) {
            message.images.push_back({.id = NextImageId(), .mimeType = attachment.mimeType, .data = attachment.text});
        }
    }
    PushTranscriptEntry(s, std::move(message));
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
        s.pendingCheckpoint = Checkpoint{
            .transcriptIndex = s.transcript.size() - 1,
            .promptPreview   = std::move(preview),
            .timestamp       = std::chrono::system_clock::now(),
        };
    }
    // chat-feel follow-up: the only on-screen change between hitting Enter
    // and the first agent_message_chunk used to be nothing at all -- reads
    // as "did this hang?" for however long the agent takes to say anything.
    // Reuses the same mode-line spinner registry LSP already drives.
    s.promptInFlight  = true;
    s.promptStartedAt = std::chrono::steady_clock::now();
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
            {"sessionId", s.id},
            {"prompt", promptBlocks},
        },
        [this, key = s.key](std::optional<Json> result, std::optional<Json> error) {
            Session* found = SessionByKey(key);
            if (!found) {
                return; // closed mid-turn; CloseSession already settled it
            }
            Session&   s       = *found;
            const auto elapsed = s.promptStartedAt ? std::chrono::steady_clock::now() - *s.promptStartedAt : std::chrono::steady_clock::duration{};
            s.promptInFlight   = false;
            s.promptStartedAt.reset();
            editor::EndBackgroundActivity(kAcpActivity);
            FinalizePendingCheckpoint(s);
            StopToolTimers(s);
            if (error && IsAuthRequired(*error)) {
                RequireLogin();
                return;
            }
            if (error) {
                const std::string message = "error: " + StringField(*error, "message", std::string("prompt failed"));
                AppendToOutputBuffer("\n[" + message + "]\n");
                PushSessionEvent(s, message);
                NoteAttention(s, Attention::TurnFinished, elapsed);
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
                PushSessionEvent(s, stopReason);
            }
            // The next queued prompt goes out once a turn ends on its own. A
            // cancelled turn holds the queue: the user interrupted, and the
            // panel hands the queued drafts back to the composer.
            if (stopReason != "cancelled" && !s.queuedPrompts.empty() && state_ == SessionState::Active) {
                QueuedPrompt next = std::move(s.queuedPrompts.front());
                s.queuedPrompts.pop_front();
                SendPrompt(s, next.text, next.attachments);
            }
            else if (stopReason != "cancelled") {
                NoteAttention(s, Attention::TurnFinished, elapsed);
            }
        });
    return "Sent.";
}

void Manager::QueuePrompt(QueuedPrompt prompt) {
    Session& s = *current_;
    s.queuedPrompts.push_back(std::move(prompt));
}

const std::deque<Manager::QueuedPrompt>& Manager::QueuedPrompts() const {
    const Session& s = *current_;
    return s.queuedPrompts;
}

std::optional<Manager::QueuedPrompt> Manager::TakeLastQueued() {
    Session& s = *current_;
    if (s.queuedPrompts.empty()) {
        return std::nullopt;
    }
    QueuedPrompt last = std::move(s.queuedPrompts.back());
    s.queuedPrompts.pop_back();
    return last;
}

std::vector<Manager::QueuedPrompt> Manager::TakeQueue() {
    Session&                  s = *current_;
    std::vector<QueuedPrompt> all(std::make_move_iterator(s.queuedPrompts.begin()), std::make_move_iterator(s.queuedPrompts.end()));
    s.queuedPrompts.clear();
    return all;
}

bool Manager::SupportsSteering() const {
    return agentSupportsSteering_;
}

bool Manager::SupportsImages() const {
    return agentSupportsImages_;
}

std::string Manager::Steer(QueuedPrompt prompt) {
    Session& s = *current_;
    if (state_ != SessionState::Active || !client_) {
        return "No active ACP session (see acp-start-session).";
    }
    if (!s.promptInFlight) {
        return SendPrompt(s, prompt.text, prompt.attachments);
    }
    if (!agentSupportsSteering_) {
        s.queuedPrompts.push_back(std::move(prompt));
        return "This agent can't take a message mid-turn; queued for when it finishes.";
    }
    const Json blocks = PromptBlocks(prompt.text, prompt.attachments);
    client_->SendRequest(
        "_session/steering",
        Json{{"sessionId", s.id}, {"prompt", blocks}, {"_meta", {{"steering", {{"idleBehavior", "promptRequired"}}}}}},
        [this, key = s.key, prompt = std::move(prompt)](std::optional<Json> result, std::optional<Json> error) mutable {
            Session* found = SessionByKey(key);
            if (!found) {
                return;
            }
            Session& s = *found;
            if (error) {
                PushSessionEvent(s, "steering failed: " + StringField(*error, "message", std::string("unknown error")) + " -- queued instead");
                s.queuedPrompts.push_back(std::move(prompt));
                return;
            }
            const std::string outcome = result && result->is_object() ? StringField(*result, "outcome", std::string()) : std::string();
            if (outcome == "promptRequired") {
                // The turn ended before the message could join it.
                if (s.promptInFlight) {
                    s.queuedPrompts.push_back(std::move(prompt));
                }
                else {
                    SendPrompt(s, prompt.text, prompt.attachments);
                }
                return;
            }
            AppendToOutputBuffer("\n>> " + prompt.text + "\n");
            PushTranscriptEntry(s, TranscriptEntry{.kind = TranscriptEntry::Kind::UserMessage, .text = prompt.text, .status = "steered"});
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
        if (client_) {
            client_->PrepareForGracefulShutdown();
            for (const auto& session : sessions_) {
                if (!session->id.empty()) {
                    client_->SendRequest("session/close", Json{{"sessionId", session->id}}, [](std::optional<Json>, std::optional<Json>) {});
                }
            }
        }
    }
    catch (const std::exception&) {
        // EPIPE from an already-dead agent -- teardown proceeds regardless.
    }
    EndSession("ACP session stopped.");
    return "ACP session stopped.";
}

bool Manager::CancelPrompt() {
    Session& s = *current_;
    if (state_ != SessionState::Active || !s.promptInFlight || !client_) {
        return false;
    }
    client_->SendNotification("session/cancel", Json{{"sessionId", s.id}});
    return true;
}

bool Manager::PromptInFlight() const {
    const Session& s = *current_;
    return s.promptInFlight;
}

void Manager::RecordCheckpointFileEdit(Session& s, text::Buffer& buffer, const std::filesystem::path& path, std::size_t beforeSequence) {
    if (!s.pendingCheckpoint) {
        return;
    }
    // A second (or later) write to the same path within one turn extends
    // the existing record's afterSequence rather than adding a duplicate --
    // beforeSequence must stay the sequence from *before this turn's first*
    // write to it, not this write's own.
    for (CheckpointFileRecord& record : s.pendingCheckpoint->fileRecords) {
        if (record.path == path) {
            record.afterSequence = buffer.CurrentUndoSequence();
            return;
        }
    }
    s.pendingCheckpoint->fileRecords.push_back(CheckpointFileRecord{
        .path           = path,
        .beforeSequence = beforeSequence,
        .afterSequence  = buffer.CurrentUndoSequence(),
    });
}

void Manager::SnapshotBeforeEdit(Session& s, const std::string& path) {
    if (!s.pendingCheckpoint || path.empty()) {
        return;
    }
    std::filesystem::path resolved(path);
    if (resolved.is_relative()) {
        resolved = editor::ProjectRoot() / resolved;
    }
    resolved = resolved.lexically_normal();
    for (const TurnFile& file : s.pendingCheckpoint->files) {
        if (file.path == resolved) {
            return;
        }
    }
    if (std::optional<FileState> before = ReadTurnFile(bufferList_, resolved)) {
        s.pendingCheckpoint->files.push_back(TurnFile{.path = resolved, .before = std::move(*before), .after = std::nullopt});
    }
}

void Manager::SnapshotToolCallFiles(Session& s, const TranscriptEntry& toolCall) {
    // Only calls that say they change files: a read or a search names paths too.
    if (toolCall.toolKind != "edit" && toolCall.toolKind != "delete" && toolCall.toolKind != "move") {
        return;
    }
    for (const ToolLocation& location : toolCall.locations) {
        SnapshotBeforeEdit(s, location.path);
    }
}

void Manager::FinalizePendingCheckpoint(Session& s) {
    if (s.pendingCheckpoint) {
        std::size_t changedFiles = 0;
        std::size_t added        = 0;
        std::size_t removed      = 0;
        for (TurnFile& file : s.pendingCheckpoint->files) {
            file.after                          = ReadTurnFile(bufferList_, file.path);
            const std::vector<ReviewHunk> hunks = TurnHunks(file, 0);
            changedFiles += hunks.empty() ? 0 : 1;
            for (const ReviewHunk& hunk : hunks) {
                added += hunk.newLines.size();
                removed += hunk.oldLines.size();
            }
        }
        if (changedFiles > 0) {
            // The panel makes this line open the turn's review.
            PushTranscriptEntry(s, TranscriptEntry{.kind   = TranscriptEntry::Kind::SessionEvent,
                                                   .text   = std::to_string(changedFiles) + (changedFiles == 1 ? " file" : " files") +
                                                             " changed (+" + std::to_string(added) + " −" + std::to_string(removed) + ")",
                                                   .status = "review"});
        }
        s.checkpoints.push_back(std::move(*s.pendingCheckpoint));
        s.pendingCheckpoint.reset();
    }
}

std::size_t Manager::CheckpointCount() const {
    const Session& s = *current_;
    return s.checkpoints.size();
}

const Manager::Checkpoint& Manager::CheckpointAt(std::size_t index) const {
    const Session& s = *current_;
    return s.checkpoints.at(index);
}

Manager::RewindOutcome Manager::RewindTo(std::size_t index) {
    Session&      s = *current_;
    RewindOutcome outcome;
    if (index >= s.checkpoints.size()) {
        return outcome;
    }
    outcome.description  = s.checkpoints[index].promptPreview;
    outcome.turnsRewound = s.checkpoints.size() - index;

    // Newest-first: a file touched by more than one of the turns being
    // rewound is walked back one hop at a time, so an intermediate turn's
    // own beforeSequence/afterSequence pair still has to match up with its
    // neighbor for the chain to continue -- exactly ProjectUndoManager's own
    // divergence check, just applied repeatedly instead of once.
    for (std::size_t i = s.checkpoints.size(); i-- > index;) {
        for (const CheckpointFileRecord& record : s.checkpoints[i].fileRecords) {
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
        for (const std::filesystem::path& path : s.checkpoints[i].untrackedPaths) {
            const bool snapshotted = std::any_of(s.checkpoints[i].files.begin(), s.checkpoints[i].files.end(),
                                                 [&](const TurnFile& file) { return file.path == path; });
            if (!snapshotted) {
                outcome.untrackedFiles.push_back(path.string());
            }
        }
        // Files the agent changed itself: restored from the turn's snapshot
        // when they still read as the turn left them.
        for (const TurnFile& file : s.checkpoints[i].files) {
            const bool undoTracked = std::any_of(s.checkpoints[i].fileRecords.begin(), s.checkpoints[i].fileRecords.end(),
                                                 [&](const CheckpointFileRecord& record) { return record.path == file.path; });
            if (undoTracked) {
                continue;
            }
            const std::optional<FileState> current = ReadTurnFile(bufferList_, file.path);
            if (!current || !file.after || *current != *file.after) {
                outcome.divergedFiles.push_back(file.path.string());
                continue;
            }
            try {
                WriteTurnFile(bufferList_, file.path, file.before);
                outcome.revertedFiles.push_back(file.path.string());
            }
            catch (const std::exception&) {
                outcome.divergedFiles.push_back(file.path.string());
            }
        }
    }

    const std::size_t truncateAt = s.checkpoints[index].transcriptIndex;
    if (truncateAt < s.transcript.size()) {
        s.transcript.erase(s.transcript.begin() + static_cast<std::ptrdiff_t>(truncateAt), s.transcript.end());
        Bump(s);
    }
    s.checkpoints.erase(s.checkpoints.begin() + static_cast<std::ptrdiff_t>(index), s.checkpoints.end());
    s.livePlanEntryIndex.reset(); // may have pointed past the new tail

    std::string summary = "rewound " + std::to_string(outcome.turnsRewound) + " turn(s) to before \"" + outcome.description +
                          "\" -- " + std::to_string(outcome.revertedFiles.size()) + " file(s) reverted";
    if (!outcome.divergedFiles.empty()) {
        summary += ", " + std::to_string(outcome.divergedFiles.size()) + " skipped (edited since)";
    }
    if (!outcome.untrackedFiles.empty()) {
        summary += ", " + std::to_string(outcome.untrackedFiles.size()) + " unaffected (not open in ned -- see backup history)";
    }
    PushSessionEvent(s, summary);
    return outcome;
}

void Manager::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    // The turn waits on the user while a question is open, however long.
    const bool waitingOnUser = std::any_of(sessions_.begin(), sessions_.end(), [](const auto& session) {
        return session->pendingPermissionPrompt.has_value() || session->pendingElicitation.has_value();
    });
    if (client_ && !waitingOnUser) {
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
        // A write for a closed session still lands; it just has no turn to join.
        Session  detached;
        Session* found = SessionFor(params);
        Session& s     = found ? *found : detached;
        // ACP checkpoint/rewind follow-up: captured before the write lands,
        // so a buffer already open in ned can be jumped straight back to
        // this exact undo node later -- see RecordCheckpointFileEdit. A
        // buffer not currently open can't be undo-tracked at all; the best
        // this can do for it is preserve the prior on-disk content as a
        // Backup.h version before it's clobbered below, for manual recovery.
        SnapshotBeforeEdit(s, pathStr);
        text::Buffer*              buffer = bufferList_.FindByPath(pathStr);
        std::optional<std::size_t> beforeSequence;
        if (buffer && !buffer->IsLoading()) {
            beforeSequence = buffer->CurrentUndoSequence();
        }
        else if (s.pendingCheckpoint) {
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
        if (s.pendingCheckpoint) {
            if (buffer && beforeSequence) {
                RecordCheckpointFileEdit(s, *buffer, pathStr, *beforeSequence);
            }
            else if (!buffer) {
                s.pendingCheckpoint->untrackedPaths.emplace_back(pathStr);
            }
        }
        respond(Json::object(), std::nullopt);
    });

    client.SetRequestHandler("elicitation/create", [this](const Json& params, RespondFn respond) {
        Session* found = SessionFor(params);
        if (!found || found->pendingElicitation || !params.is_object()) {
            respond(Json{{"action", "cancel"}}, std::nullopt);
            return;
        }
        Session&    s = *found;
        Elicitation elicitation{.id      = ++elicitationCount_,
                                .mode    = StringField(params, "mode", std::string("form")),
                                .message = StringField(params, "message", std::string())};
        if (elicitation.mode == "form") {
            elicitation.schema = params.contains("requestedSchema") && params["requestedSchema"].is_object() ? params["requestedSchema"] : Json::object();
            if (client_) {
                elicitation.fieldOrder = SchemaPropertyOrder(client_->CurrentFrame());
            }
        }
        else if (elicitation.mode == "url") {
            elicitation.url           = StringField(params, "url", std::string());
            elicitation.elicitationId = StringField(params, "elicitationId", std::string());
        }
        else {
            respond(Json{{"action", "decline"}}, std::nullopt); // a mode this client doesn't know
            return;
        }
        elicitation.toolCallId = StringField(params, "toolCallId", std::string());
        // Time spent waiting on the user isn't the tool's.
        if (TranscriptEntry* call = FindToolCall(s, elicitation.toolCallId)) {
            call->startedAt.reset();
        }
        s.pendingElicitation        = elicitation;
        s.pendingElicitationRespond = std::move(respond);
        PushTranscriptEntry(s, TranscriptEntry{.kind = TranscriptEntry::Kind::SessionEvent, .text = elicitation.message, .status = "question"});
        NoteAttention(s, Attention::QuestionAsked,
                      s.promptStartedAt ? std::chrono::steady_clock::now() - *s.promptStartedAt : std::chrono::steady_clock::duration{});
    });

    client.SetNotificationHandler("elicitation/complete", [this](const Json& params) {
        const std::string id = StringField(params, "elicitationId", std::string());
        for (const auto& session : sessions_) {
            Session&   s  = *session;
            const auto it = std::find_if(s.openUrlElicitations.begin(), s.openUrlElicitations.end(),
                                         [&](const auto& open) { return open.first == id; });
            if (it != s.openUrlElicitations.end()) {
                PushSessionEvent(s, "done: " + it->second);
                s.openUrlElicitations.erase(it);
                return;
            }
        }
    });

    client.SetRequestHandler("session/request_permission", [this](const Json& params, RespondFn respond) {
        Session* found = SessionFor(params);
        if (!found) {
            respond(Json{{"outcome", {{"outcome", "cancelled"}}}}, std::nullopt);
            return;
        }
        Session&         s = *found;
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
        s.permissionToolCallId = params.contains("toolCall") && params["toolCall"].is_object()
                                     ? StringField(params["toolCall"], "toolCallId", std::string())
                                     : std::string();
        if (TranscriptEntry* call = FindToolCall(s, s.permissionToolCallId)) {
            call->startedAt.reset();
        }
        // An edit asking permission hasn't run yet -- the surest moment to
        // record what it's about to change.
        if (params.contains("toolCall") && params["toolCall"].is_object()) {
            TranscriptEntry asked{.kind = TranscriptEntry::Kind::ToolCall};
            asked.toolKind  = StringField(params["toolCall"], "kind", std::string());
            asked.locations = params["toolCall"].contains("locations") ? ParseLocations(params["toolCall"]["locations"])
                                                                       : std::vector<ToolLocation>{};
            SnapshotToolCallFiles(s, asked);
        }
        s.pendingPermissionPrompt  = prompt;
        s.pendingPermissionRespond = std::move(respond);
        AppendToOutputBuffer("\n[permission requested: " + prompt.description + "]\n");
        PushTranscriptEntry(s, TranscriptEntry{.kind = TranscriptEntry::Kind::Permission, .text = prompt.description});
        // A session that isn't shown waits for its tab to be picked.
        if (onPermissionRequest_ && &s == current_) {
            onPermissionRequest_(prompt);
        }
        NoteAttention(s, Attention::PermissionRequested,
                      s.promptStartedAt ? std::chrono::steady_clock::now() - *s.promptStartedAt : std::chrono::steady_clock::duration{});
    });

    client.SetOnDisconnected([this](std::string reason) { EndSession("ACP agent disconnected: " + reason); });
}

void Manager::HandleSessionUpdate(const Json& params) {
    if (!params.contains("update") || !params["update"].is_object()) {
        return;
    }
    // Stray updates for a session this client replaced (a resume) or
    // closed are not ours.
    Session* found = SessionFor(params);
    if (!found) {
        return;
    }
    Session&          s      = *found;
    const Json&       update = params["update"];
    const std::string kind   = StringField(update, "sessionUpdate", std::string());

    // A new conversation has no history yet, and a fork's is already copied
    // in; some agents (opencode) replay the fork's history before answering.
    if (s.creating && (kind == "agent_message_chunk" || kind == "agent_thought_chunk" || kind == "user_message_chunk" ||
                       kind == "tool_call" || kind == "tool_call_update" || kind == "plan")) {
        return;
    }

    if (kind == "agent_message_chunk" || kind == "agent_thought_chunk" || kind == "user_message_chunk") {
        const Json content = update.contains("content") && update["content"].is_object() ? update["content"] : Json::object();
        if (StringField(content, "type", std::string()) != "text") {
            if (kind == "agent_message_chunk") {
                if (std::optional<TranscriptEntry> entry = AgentContentEntry(content)) {
                    entry->parentToolCallId = ParentToolCallId(update);
                    PushTranscriptEntry(s, std::move(*entry));
                }
            }
            else if (kind == "user_message_chunk" && s.replaying) {
                const std::string name = AttachmentName(content);
                if (name.empty()) {
                    return;
                }
                const std::string messageId = StringField(update, "messageId", std::string());
                if (s.transcript.empty() || s.transcript.back().kind != TranscriptEntry::Kind::UserMessage || messageId != s.replayUserMessageId) {
                    PushTranscriptEntry(s, TranscriptEntry{.kind = TranscriptEntry::Kind::UserMessage});
                }
                AppendAttachmentName(s.transcript.back().text, name);
                if (std::optional<TranscriptImage> image = ImageFromBlock(content)) {
                    s.transcript.back().images.push_back(std::move(*image));
                }
                s.replayUserMessageId = messageId;
                Bump(s);
                NotifyTranscriptChanged();
            }
            return;
        }
        const std::string text = StringField(content, "text", std::string());
        AppendToOutputBuffer(text);
        // user_message_chunk is the agent echoing what SendPrompt
        // already pushed as one clean Kind::UserMessage entry --
        // coalescing it here too would duplicate that entry.
        // agent_thought_chunk is routed to its own Kind (AgentThought)
        // rather than folded into AgentText -- see TranscriptEntry::Kind's
        // own doc comment.
        if (kind == "user_message_chunk" && s.replaying) {
            // A replayed prompt. Consecutive chunks of one message
            // coalesce; a new messageId starts the next.
            const std::string messageId = StringField(update, "messageId", std::string());
            if (!s.transcript.empty() && s.transcript.back().kind == TranscriptEntry::Kind::UserMessage && messageId == s.replayUserMessageId) {
                s.transcript.back().text += text;
                Bump(s);
                NotifyTranscriptChanged();
            }
            else {
                PushTranscriptEntry(s, TranscriptEntry{.kind = TranscriptEntry::Kind::UserMessage, .text = text});
            }
            s.replayUserMessageId = messageId;
        }
        else if (kind == "agent_thought_chunk") {
            PushOrAppendAgentText(s, TranscriptEntry::Kind::AgentThought, text, StringField(update, "messageId"), ParentToolCallId(update));
        }
        else if (kind == "agent_message_chunk") {
            PushOrAppendAgentText(s, TranscriptEntry::Kind::AgentText, text, StringField(update, "messageId"), ParentToolCallId(update));
        }
        return;
    }
    if (kind == "tool_call" || kind == "tool_call_update") {
        const std::string title = StringField(update, "title", StringField(update, "kind", std::string("tool call")));
        AppendToOutputBuffer("\n[tool: " + title + "]\n");
        PushOrUpdateToolCall(s, update);
        return;
    }
    if (kind == "plan") {
        PushOrReplacePlan(s, update);
        return;
    }
    if (kind == "current_mode_update") {
        const std::string modeId = StringField(update, "currentModeId", std::string());
        if (!modeId.empty() && modeId != s.currentModeId) {
            ApplyCurrentModeId(s, modeId);
            const auto mode = std::find_if(s.modes.begin(), s.modes.end(), [&modeId](const SessionMode& m) { return m.id == modeId; });
            PushSessionEvent(s, "mode: " + (mode == s.modes.end() ? modeId : mode->name));
        }
        return;
    }
    if (kind == "available_commands_update") {
        s.availableCommands.clear();
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
            s.availableCommands.push_back({.name        = command["name"].get<std::string>(),
                                           .description = StringField(command, "description", std::string()),
                                           .inputHint   = std::move(hint)});
        }
        return;
    }
    if (kind == "config_option_update") {
        ApplyConfigOptions(s, update.value("configOptions", Json::array()));
        return;
    }
    if (kind == "session_info_update") {
        if (update.contains("title") && update["title"].is_string()) {
            s.title = update["title"].get<std::string>();
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
        s.usage = std::move(usage);
        return;
    }
    if (kind == "notice") {
        const std::string title = StringField(update, "title", std::string());
        if (title.empty()) {
            return;
        }
        const std::string description = StringField(update, "description", std::string());
        AppendToOutputBuffer("\n[" + StringField(update, "severity", std::string("info")) + "] " + title +
                             (description.empty() ? std::string() : ": " + description) + "\n");
        PushTranscriptEntry(s, TranscriptEntry{.kind   = TranscriptEntry::Kind::Notice,
                                               .text   = title,
                                               .status = StringField(update, "severity", std::string("info")),
                                               .detail = description});
        return;
    }
    if (kind == "compaction_update" || kind == "compaction_summary_chunk") {
        UpdateCompaction(s, kind, update);
        return;
    }
    // Unrecognized/forward-compatible update kind -- see this class's own
    // header comment on why this isn't treated as an error.
}

void Manager::UpdateCompaction(Session& s, const std::string& kind, const Json& update) {
    const std::string compactionId = StringField(update, "compactionId", std::string());
    if (compactionId.empty()) {
        return;
    }
    const auto found = std::find_if(s.transcript.rbegin(), s.transcript.rend(), [&compactionId](const TranscriptEntry& entry) {
        return entry.kind == TranscriptEntry::Kind::Compaction && entry.itemId == compactionId;
    });
    if (kind == "compaction_summary_chunk") {
        if (found != s.transcript.rend() && update.contains("content")) {
            found->text += ContentText(update["content"]);
            Bump(s);
            NotifyTranscriptChanged();
        }
        return;
    }
    // The first update places the compaction in the transcript; later ones
    // patch it: an absent field is unchanged, null clears it.
    TranscriptEntry* entry = found == s.transcript.rend() ? nullptr : &*found;
    if (!entry) {
        PushTranscriptEntry(s, TranscriptEntry{.kind = TranscriptEntry::Kind::Compaction, .itemId = compactionId});
        entry = &s.transcript.back();
    }
    entry->status = StringField(update, "status", entry->status);
    if (update.contains("summary")) {
        entry->text = ContentText(update["summary"]);
    }
    if (update.contains("error")) {
        entry->detail = StringField(update, "error", std::string());
    }
    AppendToOutputBuffer("\n[compaction " + entry->status + "]\n");
    Bump(s);
    NotifyTranscriptChanged();
}

void Manager::EndSession(std::string reason) {
    if (state_ == SessionState::Inactive) {
        return; // e.g. disconnect EOF arriving after an explicit StopSession already tore down
    }
    state_ = SessionState::Inactive;
    listSessionsRequest_.reset();
    loginRequired_      = false;
    sessionAwaitsLogin_ = false;
    for (const auto& session : sessions_) {
        Session& s  = *session;
        s.replaying = false;
        if (s.promptInFlight) {
            // A prompt was still outstanding when the session ended out from
            // under it (StopSession mid-turn, or the agent disconnecting) --
            // its own SendRequest callback is now abandoned (Client's
            // documented "dropped, uninvoked" shutdown contract), so nothing
            // else would ever end this spinner.
            s.promptInFlight = false;
            editor::EndBackgroundActivity(kAcpActivity);
            FinalizePendingCheckpoint(s);
        }
        StopToolTimers(s);
        s.pendingElicitation.reset();
        s.pendingElicitationRespond = nullptr;
        s.openUrlElicitations.clear();
        for (const QueuedPrompt& dropped : s.queuedPrompts) {
            PushSessionEvent(s, "not sent: " + dropped.text);
        }
        s.queuedPrompts.clear();
        s.id.clear();
        s.availableCommands.clear();
        s.modes.clear();
        s.currentModeId.clear();
        s.configOptions.clear();
        s.title.clear();
        s.usage.reset();
        s.promptStartedAt.reset();
        s.pendingPermissionPrompt.reset();
        s.pendingPermissionRespond = nullptr;
        s.livePlanEntryIndex.reset();
    }
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
    for (const auto& session : sessions_) {
        PushSessionEvent(*session, reason);
    }
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
    Session& s = *current_;
    if (!s.pendingPermissionRespond) {
        return;
    }
    // acp-panel-permission-resolution follow-up: confirmed live via ASan --
    // AcpPanel::OnEvent's own caller passes pending.options[index].optionId,
    // a reference *into* s.pendingPermissionPrompt itself (the very object
    // .reset() below destroys). Copy first so every use below reads a
    // stable, independent string instead of freed memory.
    const std::string optionIdCopy = optionId;
    RespondFn         respond      = std::move(s.pendingPermissionRespond);
    std::string       chosen       = optionIdCopy;
    for (const PermissionOption& option : s.pendingPermissionPrompt->options) {
        if (option.optionId != optionIdCopy) {
            continue;
        }
        if (!option.name.empty()) {
            chosen = option.name;
        }
        TranscriptEntry* call = FindToolCall(s, s.permissionToolCallId);
        if (option.kind.starts_with("allow") && call && !call->finishedAt && !s.replaying) {
            call->startedAt = std::chrono::steady_clock::now();
        }
    }
    s.pendingPermissionPrompt.reset();
    AppendToOutputBuffer("[selected: " + optionIdCopy + "]\n");
    PushSessionEvent(s, "selected: " + chosen);
    respond(Json{{"outcome", {{"outcome", "selected"}, {"optionId", optionIdCopy}}}}, std::nullopt);
}

void Manager::CancelPermissionPrompt() {
    Session& s = *current_;
    if (!s.pendingPermissionRespond) {
        return;
    }
    RespondFn respond = std::move(s.pendingPermissionRespond);
    s.pendingPermissionPrompt.reset();
    AppendToOutputBuffer("[permission cancelled]\n");
    PushSessionEvent(s, "permission cancelled");
    respond(Json{{"outcome", {{"outcome", "cancelled"}}}}, std::nullopt);
}

const std::optional<Manager::PermissionPrompt>& Manager::PendingPermissionPrompt() const {
    const Session& s = *current_;
    return s.pendingPermissionPrompt;
}

const std::optional<Manager::Elicitation>& Manager::PendingElicitation() const {
    const Session& s = *current_;
    return s.pendingElicitation;
}

void Manager::AnswerElicitation(const std::string& action, const Json& content, const std::string& summary) {
    Session& s = *current_;
    if (!s.pendingElicitationRespond || !s.pendingElicitation) {
        return;
    }
    RespondFn         respond     = std::move(s.pendingElicitationRespond);
    const Elicitation elicitation = std::move(*s.pendingElicitation);
    s.pendingElicitation.reset();
    Json result{{"action", action}};
    if (action == "accept" && elicitation.mode == "form") {
        result["content"] = content;
    }
    if (action == "accept" && elicitation.mode == "url" && !elicitation.elicitationId.empty()) {
        s.openUrlElicitations.emplace_back(elicitation.elicitationId, elicitation.message);
    }
    PushSessionEvent(s, action == "accept"    ? (summary.empty() ? std::string("answered") : "answered: " + summary)
                        : action == "decline" ? std::string("skipped the question")
                                              : std::string("question cancelled"));
    respond(result, std::nullopt);
}

void Manager::SetOnSessionEnded(std::function<void(std::string)> handler) {
    onSessionEnded_ = std::move(handler);
}

Client& Manager::SetClientForTesting(std::unique_ptr<Client> client) {
    client_ = std::move(client);
    return *client_;
}

} // namespace ned::editor::acp
