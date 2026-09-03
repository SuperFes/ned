#include "AcpManager.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include "AcpConfig.h"
#include "Editor/BackgroundActivity.h"
#include "Editor/Backup.h"
#include "Editor/ProjectRoot.h"
#include "Editor/WrapOverrides.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::acp {

namespace {

    std::string AcpOutputBufferName(std::string_view agentName) {
        return "*acp: " + std::string(agentName) + "*";
    }

    // Mode-line spinner name for a prompt in flight -- see AcpManager::
    // PromptInFlight's doc comment. String, not string_view, to match
    // BackgroundActivity's own std::string parameters (LspClient.cpp's
    // kLspActivity does the same conversion for the same reason).
    const std::string kAcpActivity{"ACP"};

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

AcpManager::~AcpManager() {
    // See the header's doc comment -- EndSession normally does this, but a
    // destructor that runs without one first (a session/prompt request
    // abandoned mid-flight, e.g. an owning panel torn down directly) must
    // not leak the "ACP" mode-line spinner for the rest of the process.
    if (promptInFlight_) {
        editor::EndBackgroundActivity(kAcpActivity);
    }
}

AcpManager::SessionState AcpManager::State() const {
    return state_;
}

const std::string& AcpManager::AgentName() const {
    return agentName_;
}

const std::vector<AcpManager::TranscriptEntry>& AcpManager::Transcript() const {
    return transcript_;
}

std::size_t AcpManager::TranscriptGeneration() const {
    return transcriptGeneration_;
}

void AcpManager::SetOnTranscriptChanged(std::function<void()> handler) {
    onTranscriptChanged_ = std::move(handler);
}

text::Buffer& AcpManager::OutputBuffer(const std::string& agentName) {
    const std::string bufferName = AcpOutputBufferName(agentName);
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

void AcpManager::AppendToOutputBuffer(std::string_view text) {
    if (agentName_.empty()) {
        return; // no session has ever started -- nothing to append to
    }
    OutputBuffer(agentName_).AppendWhileReadOnly(text);
}

void AcpManager::PushTranscriptEntry(TranscriptEntry entry) {
    transcript_.push_back(std::move(entry));
    ++transcriptGeneration_;
    NotifyTranscriptChanged();
}

void AcpManager::PushSessionEvent(std::string text) {
    PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::SessionEvent, .text = std::move(text)});
}

void AcpManager::NotifyTranscriptChanged() {
    if (onTranscriptChanged_) {
        onTranscriptChanged_();
    }
}

void AcpManager::PushOrAppendAgentText(TranscriptEntry::Kind kind, std::string_view text) {
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

void AcpManager::PushOrUpdateToolCall(const Json& update) {
    // A real agent's tool_call_update frequently omits title/status entirely
    // (confirmed live against Claude Code's ACP adapter -- a follow-up update
    // often carries only content/rawOutput for an already-known toolCallId).
    // Absence must mean "unchanged," not "reset to the generic fallback" --
    // ROADMAP.md's own prediction that this parsing would need widening once
    // exercised against a real agent.
    const bool        hasTitle   = update.contains("title") || update.contains("kind");
    const std::string title      = update.value("title", update.value("kind", std::string("tool call")));
    const bool        hasStatus  = update.contains("status");
    const std::string status     = update.value("status", std::string());
    const std::string toolCallId = update.value("toolCallId", std::string());

    // A "diff"-typed content item, when present, is the actual before/after
    // text of a file edit -- confirmed live against Claude Code's Edit tool
    // (ACP's own {type: "diff", path, oldText, newText} shape). Most tool
    // calls never carry one, and most updates for one that does only arrive
    // on the update that actually has it (earlier updates for the same call
    // have an empty "content": []) -- so, same as title/status, absence here
    // must mean "no new diff this update," not "clear the one we already have."
    bool        hasDiff = false;
    std::string diffOldText;
    std::string diffNewText;
    if (update.contains("content") && update["content"].is_array()) {
        for (const Json& item : update["content"]) {
            if (item.is_object() && item.value("type", std::string()) == "diff") {
                hasDiff     = true;
                diffOldText = item.value("oldText", std::string());
                diffNewText = item.value("newText", std::string());
                break;
            }
        }
    }

    if (!toolCallId.empty()) {
        for (auto it = transcript_.rbegin(); it != transcript_.rend(); ++it) {
            if (it->kind == TranscriptEntry::Kind::ToolCall && it->toolCallId == toolCallId) {
                if (hasTitle) {
                    it->text = title;
                }
                if (hasStatus) {
                    it->status = status;
                }
                if (hasDiff) {
                    it->diffOldText = diffOldText;
                    it->diffNewText = diffNewText;
                }
                ++transcriptGeneration_;
                NotifyTranscriptChanged();
                return;
            }
        }
    }
    PushTranscriptEntry(TranscriptEntry{
        .kind        = TranscriptEntry::Kind::ToolCall,
        .text        = title,
        .status      = status,
        .toolCallId  = toolCallId.empty() ? std::nullopt : std::optional<std::string>(toolCallId),
        .diffOldText = hasDiff ? std::optional<std::string>(diffOldText) : std::nullopt,
        .diffNewText = hasDiff ? std::optional<std::string>(diffNewText) : std::nullopt,
    });
}

void AcpManager::PushOrReplacePlan(const Json& update) {
    std::vector<std::string> steps;
    if (update.contains("entries") && update["entries"].is_array()) {
        for (const Json& entryJson : update["entries"]) {
            std::string content;
            if (entryJson.contains("content")) {
                if (entryJson["content"].is_string()) {
                    content = entryJson["content"].get<std::string>();
                }
                else if (entryJson["content"].is_object()) {
                    content = entryJson["content"].value("text", std::string());
                }
            }
            if (content.empty()) {
                content = entryJson.value("description", std::string());
            }
            const std::string status = entryJson.value("status", std::string());
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

text::Buffer* AcpManager::StartSession(const std::string& agentName) {
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
        const auto argv = AcpAgentCommand(agentName);
        if (!argv) {
            const std::string message = "No command configured for ACP agent \"" + agentName + "\" (see ned/set-acp-agent).";
            buffer.AppendWhileReadOnly("\n" + message + "\n");
            PushSessionEvent(message);
            return &buffer;
        }
        try {
            client_ = std::make_unique<AcpClient>(*argv, eventLoop_);
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
                const std::string message = "ACP initialize failed: " + error->value("message", std::string("unknown error"));
                AppendToOutputBuffer("\n" + message + "\n");
                PushSessionEvent(message);
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
                        const std::string message =
                            "session/new failed" + (newError ? (": " + newError->value("message", std::string())) : std::string());
                        AppendToOutputBuffer("\n" + message + "\n");
                        PushSessionEvent(message);
                        state_ = SessionState::Inactive;
                        return;
                    }
                    sessionId_ = (*newResult)["sessionId"].get<std::string>();
                    state_     = SessionState::Active;
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

std::string AcpManager::SendPrompt(const std::string& text) {
    if (state_ != SessionState::Active) {
        return "No active ACP session (see acp-start-session).";
    }
    AppendToOutputBuffer("\n> " + text + "\n");
    PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::UserMessage, .text = text});
    // ACP checkpoint/rewind follow-up: opens this turn's checkpoint,
    // finalized by FinalizePendingCheckpoint once its response arrives
    // (below) or the session ends mid-turn (EndSession). A single-line,
    // length-capped preview -- the transcript's own UserMessage entry above
    // keeps the real text verbatim, this is only for the rewind picker's
    // compact list.
    {
        std::string preview = text;
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
    promptInFlight_ = true;
    editor::BeginBackgroundActivity(kAcpActivity);
    client_->SendRequest(
        "session/prompt",
        Json{
            {"sessionId", sessionId_},
            {"prompt", Json::array({Json{{"type", "text"}, {"text", text}}})},
        },
        [this](std::optional<Json> result, std::optional<Json> error) {
            promptInFlight_ = false;
            editor::EndBackgroundActivity(kAcpActivity);
            FinalizePendingCheckpoint();
            if (error) {
                const std::string message = "error: " + error->value("message", std::string("prompt failed"));
                AppendToOutputBuffer("\n[" + message + "]\n");
                PushSessionEvent(message);
                return;
            }
            const std::string stopReason = result ? result->value("stopReason", std::string("end")) : std::string("end");
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
    // async-write-queue follow-up: PrepareForGracefulShutdown must be called
    // before this SendRequest -- see DapManager::StopSession's identical
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

bool AcpManager::CancelPrompt() {
    if (state_ != SessionState::Active || !promptInFlight_ || !client_) {
        return false;
    }
    client_->SendNotification("session/cancel", Json{{"sessionId", sessionId_}});
    return true;
}

bool AcpManager::PromptInFlight() const {
    return promptInFlight_;
}

void AcpManager::RecordCheckpointFileEdit(text::Buffer& buffer, const std::filesystem::path& path, std::size_t beforeSequence) {
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

void AcpManager::FinalizePendingCheckpoint() {
    if (pendingCheckpoint_) {
        checkpoints_.push_back(std::move(*pendingCheckpoint_));
        pendingCheckpoint_.reset();
    }
}

std::size_t AcpManager::CheckpointCount() const {
    return checkpoints_.size();
}

const AcpManager::Checkpoint& AcpManager::CheckpointAt(std::size_t index) const {
    return checkpoints_.at(index);
}

AcpManager::RewindOutcome AcpManager::RewindTo(std::size_t index) {
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

void AcpManager::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    if (client_ && !pendingPermissionPrompt_) {
        client_->ExpireStaleRequests(maxAge);
    }
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
        PushTranscriptEntry(TranscriptEntry{.kind = TranscriptEntry::Kind::Permission, .text = prompt.description});
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
            const std::string text = update["content"].value("text", std::string());
            AppendToOutputBuffer(text);
            // user_message_chunk is the agent echoing what SendPrompt
            // already pushed as one clean Kind::UserMessage entry --
            // coalescing it here too would duplicate that entry.
            // agent_thought_chunk is routed to its own Kind (AgentThought)
            // rather than folded into AgentText -- see TranscriptEntry::Kind's
            // own doc comment.
            if (kind == "agent_thought_chunk") {
                PushOrAppendAgentText(TranscriptEntry::Kind::AgentThought, text);
            }
            else if (kind == "agent_message_chunk") {
                PushOrAppendAgentText(TranscriptEntry::Kind::AgentText, text);
            }
        }
        return;
    }
    if (kind == "tool_call" || kind == "tool_call_update") {
        const std::string title = update.value("title", update.value("kind", std::string("tool call")));
        AppendToOutputBuffer("\n[tool: " + title + "]\n");
        PushOrUpdateToolCall(update);
        return;
    }
    if (kind == "plan") {
        PushOrReplacePlan(update);
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
    if (promptInFlight_) {
        // A prompt was still outstanding when the session ended out from
        // under it (StopSession mid-turn, or the agent disconnecting) --
        // its own SendRequest callback is now abandoned (AcpClient's
        // documented "dropped, uninvoked" shutdown contract), so nothing
        // else would ever end this spinner.
        promptInFlight_ = false;
        editor::EndBackgroundActivity(kAcpActivity);
        FinalizePendingCheckpoint();
    }
    sessionId_.clear();
    pendingPermissionPrompt_.reset();
    pendingPermissionRespond_ = nullptr;
    livePlanEntryIndex_.reset();
    // lsp-use-after-free follow-up: client_ used to move into retired_ here
    // instead of destroying in place, deferring to the next StartSession.
    // Confirmed live elsewhere in this codebase that deferring isn't what
    // actually makes this safe (LspClient's own identical pattern still
    // raced a periodic tick against a background thread's own Post()ed
    // callback for the same object) -- the real fix now lives in AcpClient
    // itself (alive_, see LspClient.h's header comment), so plain immediate
    // destruction is safe regardless of timing.
    client_.reset();
    AppendToOutputBuffer("\n[" + reason + "]\n");
    PushSessionEvent(reason);
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
    // acp-panel-permission-resolution follow-up: confirmed live via ASan --
    // AcpPanel::OnEvent's own caller passes pending.options[index].optionId,
    // a reference *into* pendingPermissionPrompt_ itself (the very object
    // .reset() below destroys). Copy first so every use below reads a
    // stable, independent string instead of freed memory.
    const std::string optionIdCopy = optionId;
    RespondFn         respond      = std::move(pendingPermissionRespond_);
    pendingPermissionPrompt_.reset();
    AppendToOutputBuffer("[selected: " + optionIdCopy + "]\n");
    PushSessionEvent("selected: " + optionIdCopy);
    respond(Json{{"outcome", {{"outcome", "selected"}, {"optionId", optionIdCopy}}}}, std::nullopt);
}

void AcpManager::CancelPermissionPrompt() {
    if (!pendingPermissionRespond_) {
        return;
    }
    RespondFn respond = std::move(pendingPermissionRespond_);
    pendingPermissionPrompt_.reset();
    AppendToOutputBuffer("[permission cancelled]\n");
    PushSessionEvent("permission cancelled");
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
