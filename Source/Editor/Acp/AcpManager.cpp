#include "AcpManager.h"

#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include "AcpConfig.h"
#include "Editor/ProjectRoot.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::acp {

namespace {

    std::string AcpOutputBufferName(std::string_view agentName) {
        return "*acp: " + std::string(agentName) + "*";
    }

    // Sibling-temp-file + rename, the same atomic-write shape
    // ProjectReplace.cpp's own ReplaceMatches uses.
    void WriteFileAtomically(const std::filesystem::path& path, const std::string& content) {
        std::filesystem::path tempPath = path;
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
        std::error_code ec;
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            throw std::runtime_error("rename failed for " + path.string() + ": " + ec.message());
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

} // namespace

AcpManager::AcpManager(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop) : bufferList_(bufferList), eventLoop_(eventLoop) {
}

AcpManager::SessionState AcpManager::State() const {
    return state_;
}

text::Buffer& AcpManager::OutputBuffer(const std::string& agentName) {
    const std::string bufferName = AcpOutputBufferName(agentName);
    text::Buffer*     buffer     = bufferList_.Find(bufferName);
    if (!buffer) {
        buffer = &bufferList_.CreateBuffer(bufferName);
        buffer->SetReadOnly(true); // must be set before the first append -- AppendWhileReadOnly's own precondition
    }
    return *buffer;
}

void AcpManager::AppendToOutputBuffer(std::string_view text) {
    if (agentName_.empty()) {
        return; // no session has ever started -- nothing to append to
    }
    OutputBuffer(agentName_).AppendWhileReadOnly(text);
}

text::Buffer* AcpManager::StartSession(const std::string& agentName) {
    text::Buffer& buffer = OutputBuffer(agentName);

    if (state_ != SessionState::Inactive) {
        buffer.AppendWhileReadOnly("\nAn ACP session (" + agentName_ + ") is already running -- acp-stop-session first.\n");
        return &buffer;
    }

    if (!buffer.Text().empty()) {
        buffer.AppendWhileReadOnly("\n--- new session ---\n");
    }

    retired_.clear(); // safe here: nothing of a previous session is on the stack during a key-driven start

    if (!client_) {
        const auto argv = AcpAgentCommand(agentName);
        if (!argv) {
            buffer.AppendWhileReadOnly("\nNo command configured for ACP agent \"" + agentName + "\" (see ned/set-acp-agent).\n");
            return &buffer;
        }
        try {
            client_ = std::make_unique<AcpClient>(*argv, eventLoop_);
        }
        catch (const std::exception& e) {
            client_.reset();
            buffer.AppendWhileReadOnly(std::string("\nFailed to start ACP agent: ") + e.what() + "\n");
            return &buffer;
        }
    }
    // else: a client injected via SetClientForTesting -- run the same handshake against it.

    agentName_ = agentName;
    state_     = SessionState::Starting;
    WireClient(*client_);

    client_->SendRequest(
        "initialize",
        Json{
            {"protocolVersion", 1},
            {"clientCapabilities", {{"fs", {{"readTextFile", true}, {"writeTextFile", true}}}}},
        },
        [this](std::optional<Json> result, std::optional<Json> error) {
            (void)result;
            if (error) {
                AppendToOutputBuffer("\nACP initialize failed: " + error->value("message", std::string("unknown error")) + "\n");
                state_ = SessionState::Inactive;
                return;
            }
            client_->SendRequest(
                "session/new",
                Json{
                    {"cwd", editor::ProjectRoot().string()},
                    {"mcpServers", Json::array()},
                },
                [this](std::optional<Json> newResult, std::optional<Json> newError) {
                    if (newError || !newResult || !newResult->contains("sessionId")) {
                        AppendToOutputBuffer("\nsession/new failed" +
                                             (newError ? (": " + newError->value("message", std::string())) : std::string()) + "\n");
                        state_ = SessionState::Inactive;
                        return;
                    }
                    sessionId_ = (*newResult)["sessionId"].get<std::string>();
                    state_     = SessionState::Active;
                    AppendToOutputBuffer("\n[session ready]\n");
                });
        });

    return &buffer;
}

std::string AcpManager::SendPrompt(const std::string& text) {
    if (state_ != SessionState::Active) {
        return "No active ACP session (see acp-start-session).";
    }
    AppendToOutputBuffer("\n> " + text + "\n");
    client_->SendRequest(
        "session/prompt",
        Json{
            {"sessionId", sessionId_},
            {"prompt", Json::array({Json{{"type", "text"}, {"text", text}}})},
        },
        [this](std::optional<Json> result, std::optional<Json> error) {
            if (error) {
                AppendToOutputBuffer("\n[error: " + error->value("message", std::string("prompt failed")) + "]\n");
                return;
            }
            const std::string stopReason = result ? result->value("stopReason", std::string("end")) : std::string("end");
            AppendToOutputBuffer("\n[" + stopReason + "]\n");
        });
    return "Sent.";
}

std::string AcpManager::StopSession() {
    if (state_ == SessionState::Inactive) {
        return "No active ACP session.";
    }
    // Best-effort polite close; teardown below must not depend on the agent
    // answering (or even still being alive to write to) -- same reasoning
    // as DapManager::StopSession.
    try {
        if (!sessionId_.empty() && client_) {
            client_->SendRequest("session/close", Json{{"sessionId", sessionId_}}, [](std::optional<Json>, std::optional<Json>) {});
        }
    }
    catch (const std::exception&) {
        // EPIPE from an already-dead agent -- teardown proceeds regardless.
    }
    EndSession("ACP session stopped.");
    return "ACP session stopped.";
}

void AcpManager::WireClient(AcpClient& client) {
    client.SetNotificationHandler("session/update", [this](const Json& params) { HandleSessionUpdate(params); });

    client.SetRequestHandler("fs/read_text_file", [this](const Json& params, RespondFn respond) {
        const std::string pathStr = params.value("path", std::string());
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
            content = SliceLines(content, params["line"].get<int>(), params.value("limit", -1));
        }
        respond(Json{{"content", content}}, std::nullopt);
    });

    client.SetRequestHandler("fs/write_text_file", [this](const Json& params, RespondFn respond) {
        const std::string pathStr = params.value("path", std::string());
        const std::string content = params.value("content", std::string());
        if (pathStr.empty()) {
            respond(std::nullopt, Json{{"code", 1}, {"message", "missing path"}});
            return;
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
        if (text::Buffer* buffer = bufferList_.FindByPath(pathStr); buffer && !buffer->IsLoading()) {
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
        respond(Json::object(), std::nullopt);
    });

    client.SetRequestHandler("session/request_permission", [this](const Json& params, RespondFn respond) {
        PermissionPrompt prompt;
        prompt.description = (params.contains("toolCall") && params["toolCall"].is_object())
                                 ? params["toolCall"].value("title", params["toolCall"].value("kind", std::string("Permission request")))
                                 : std::string("Permission request");
        if (params.contains("options") && params["options"].is_array()) {
            for (const Json& optionJson : params["options"]) {
                prompt.options.push_back(PermissionOption{
                    .optionId = optionJson.value("optionId", std::string()),
                    .name     = optionJson.value("name", std::string("option")),
                    .kind     = optionJson.value("kind", std::string()),
                });
            }
        }
        if (prompt.options.empty()) {
            // Malformed/empty options -- nothing to choose from; answer
            // cancelled immediately rather than opening a UI prompt with
            // nothing in it.
            respond(Json{{"outcome", {{"outcome", "cancelled"}}}}, std::nullopt);
            return;
        }
        pendingPermissionPrompt_  = prompt;
        pendingPermissionRespond_ = std::move(respond);
        AppendToOutputBuffer("\n[permission requested: " + prompt.description + "]\n");
        if (onPermissionRequest_) {
            onPermissionRequest_(prompt);
        }
    });

    client.SetOnDisconnected([this](std::string reason) { EndSession("ACP agent disconnected: " + reason); });
}

void AcpManager::HandleSessionUpdate(const Json& params) {
    if (!params.contains("update") || !params["update"].is_object()) {
        return;
    }
    const Json&       update = params["update"];
    const std::string kind   = update.value("sessionUpdate", std::string());

    if (kind == "agent_message_chunk" || kind == "agent_thought_chunk" || kind == "user_message_chunk") {
        if (update.contains("content") && update["content"].is_object() && update["content"].value("type", std::string()) == "text") {
            AppendToOutputBuffer(update["content"].value("text", std::string()));
        }
        return;
    }
    if (kind == "tool_call" || kind == "tool_call_update") {
        const std::string title = update.value("title", update.value("kind", std::string("tool call")));
        AppendToOutputBuffer("\n[tool: " + title + "]\n");
        return;
    }
    // Unrecognized/forward-compatible update kind -- see this class's own
    // header comment on why this isn't treated as an error.
}

void AcpManager::EndSession(std::string reason) {
    if (state_ == SessionState::Inactive) {
        return; // e.g. disconnect EOF arriving after an explicit StopSession already tore down
    }
    state_ = SessionState::Inactive;
    sessionId_.clear();
    pendingPermissionPrompt_.reset();
    pendingPermissionRespond_ = nullptr;
    if (client_) {
        retired_.push_back(std::move(client_)); // never destroyed mid-callback -- see header comment
    }
    AppendToOutputBuffer("\n[" + reason + "]\n");
    if (onSessionEnded_) {
        onSessionEnded_(std::move(reason));
    }
}

void AcpManager::SetOnPermissionRequest(std::function<void(const PermissionPrompt&)> handler) {
    onPermissionRequest_ = std::move(handler);
}

void AcpManager::ResolvePermissionPrompt(const std::string& optionId) {
    if (!pendingPermissionRespond_) {
        return;
    }
    RespondFn respond = std::move(pendingPermissionRespond_);
    pendingPermissionPrompt_.reset();
    AppendToOutputBuffer("[selected: " + optionId + "]\n");
    respond(Json{{"outcome", {{"outcome", "selected"}, {"optionId", optionId}}}}, std::nullopt);
}

void AcpManager::CancelPermissionPrompt() {
    if (!pendingPermissionRespond_) {
        return;
    }
    RespondFn respond = std::move(pendingPermissionRespond_);
    pendingPermissionPrompt_.reset();
    AppendToOutputBuffer("[permission cancelled]\n");
    respond(Json{{"outcome", {{"outcome", "cancelled"}}}}, std::nullopt);
}

const std::optional<AcpManager::PermissionPrompt>& AcpManager::PendingPermissionPrompt() const {
    return pendingPermissionPrompt_;
}

void AcpManager::SetOnSessionEnded(std::function<void(std::string)> handler) {
    onSessionEnded_ = std::move(handler);
}

AcpClient& AcpManager::SetClientForTesting(std::unique_ptr<AcpClient> client) {
    client_ = std::move(client);
    return *client_;
}

} // namespace ned::editor::acp
