//
// ACP client, slice 2. Owns the (single) running ACP agent session and
// streams the conversation into a plain, read-only, find-or-create output
// buffer ("*acp: <agent>*") -- the same TaskRunner::RunTask/
// TaskOutputBufferName convention, deliberately reused instead of a new
// widget for v1 (see ROADMAP.md's AI-assisted-editing entry). Analogous to
// Dap/DapManager.h: one session at a time, not a per-agent map -- chatting
// with an agent is a modal activity the same way debugging is, and nothing
// in v1's scope needs two agents live at once.
//
// Session shape (the ACP handshake):
//   1. spawn the agent, send `initialize` (capabilities exchange -- this
//      client declares fs.readTextFile/fs.writeTextFile true, and
//      deliberately leaves `terminal` and any elicitation capability
//      undeclared: v1 has no handler for terminal/* or elicitation/create,
//      and AcpClient answers an unhandled agent-initiated request with a
//      JSON-RPC MethodNotFound -- safe and spec-legal for any capability
//      this client never claimed to support);
//   2. on its response, send `session/new` (cwd = ProjectRoot()), storing
//      the returned sessionId;
//   3. thereafter `session/prompt` (SendPrompt) sends a message and
//      `session/update` notifications stream the agent's reply back,
//      appended into the output buffer as they arrive; `fs/read_text_file`/
//      `fs/write_text_file` and `session/request_permission` are answered
//      here (the fs bridge and permission-prompt exposure below) as the
//      agent's own requests arrive.
//
// session/update's exact sub-schema (the discriminated union of message
// chunks, tool calls, plans, mode changes, ...) is not pinned down here
// against the authoritative ACP JSON schema -- only informally documented at
// the time this was written. HandleSessionUpdate therefore parses the
// commonly-documented "sessionUpdate" discriminator defensively (a missing
// or unrecognized field degrades to a best-effort raw line rather than
// silently dropping the update) -- expect this to need widening once
// exercised against a real agent.
//

#ifndef NED_EDITOR_ACP_ACPMANAGER_H
#define NED_EDITOR_ACP_ACPMANAGER_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/ProcessTimeouts.h"
#include "UI/EventLoop.h"

#include "AcpClient.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::acp {

class AcpManager {
  public:
    // bufferList and eventLoop must both outlive this AcpManager -- same
    // requirement every sibling manager in this codebase documents.
    AcpManager(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop);
    // Ends the "ACP" background activity if a prompt is still in flight when
    // this is destroyed without ever going through EndSession -- mirrors
    // LspClient::~LspClient's identical cleanup for its own pending_ map
    // (see that destructor's comment). A real ~AcpManager() rather than
    // = default because of this.
    ~AcpManager();

    AcpManager(const AcpManager&)            = delete;
    AcpManager& operator=(const AcpManager&) = delete;

    enum class SessionState { Inactive,
                              Starting,
                              Active };
    [[nodiscard]] SessionState State() const;

    // The agent name passed to the most recent StartSession -- empty before
    // any session has ever started. Panel header display only.
    [[nodiscard]] const std::string& AgentName() const;

    // A structured, UI-renderable view of the session alongside the flat
    // "*acp: <agent>*" output buffer (AppendToOutputBuffer/OutputBuffer
    // above) -- that buffer stays exactly as it was for v1 consumers;
    // this is additive, for AcpPanel. Wholesale-replace-in-place semantics
    // for Plan/ToolCall entries, matching Buffer::Diagnostic's own
    // SetDiagnostics precedent, since a "plan" or "tool_call_update" is a
    // fresh authoritative snapshot of that one plan/tool-call, not an
    // independent new event.
    struct TranscriptEntry {
        // ACP chat-feel round 2: AgentThought is its own Kind, not a bool
        // tacked onto AgentText -- session/update's agent_thought_chunk and
        // agent_message_chunk were previously coalesced into the very same
        // transcript entry (PushOrAppendAgentText didn't distinguish them at
        // all), so a reply read as one undifferentiated stream of tokens with
        // no visual seam between an agent's private reasoning and its actual
        // answer. Kept as a separate Kind (matching how every other entry
        // shape here is modeled) rather than a flag so AcpPanel's own
        // per-Kind styling switch needs no special-casing.
        enum class Kind { UserMessage,
                          AgentText,
                          AgentThought,
                          ToolCall,
                          Plan,
                          Permission,
                          SessionEvent };
        Kind                       kind;
        std::string                text;       // message text / tool-call title / session event text / permission description
        std::string                status;     // tool-call or plan status, best-effort, may be empty
        std::vector<std::string>   planSteps;  // Kind::Plan only
        std::optional<std::string> toolCallId; // Kind::ToolCall only, for tool_call_update matching
        // Kind::ToolCall only -- a "diff"-typed content item (ACP's own
        // {type: "diff", path, oldText, newText} shape, confirmed live
        // against Claude Code's adapter for its Edit tool). Absent for any
        // tool call that never carries one (most don't). AcpPanel renders a
        // compact line-count summary from these rather than a full diff view
        // -- see AcpPanel.cpp's own comment on why that's deliberately not
        // attempted here.
        std::optional<std::string> diffOldText;
        std::optional<std::string> diffNewText;
    };
    [[nodiscard]] const std::vector<TranscriptEntry>& Transcript() const;
    // Bumped on every Transcript()-affecting mutation -- cheap change
    // detection for a UI cache, mirroring Buffer::DiagnosticsGeneration.
    [[nodiscard]] std::size_t TranscriptGeneration() const;
    // Fires after every Transcript()-affecting mutation. Connect-after-
    // construction, unset is a safe no-op, this class's usual convention.
    void SetOnTranscriptChanged(std::function<void()> handler);

    // Finds-or-creates "*acp: <agent>*" (SetReadOnly(true) before the first
    // append; a "--- new session ---" separator is appended first if the
    // buffer already has content from a prior session -- TaskRunner::RunTask's
    // own re-run convention) and always returns it, even on failure (no
    // configured command, spawn failure, a session already running) --
    // mirrors TaskRunner::RunTask's own "always return the buffer" contract
    // so the caller can switch to it unconditionally. The handshake
    // (initialize -> session/new) runs asynchronously; failures and the
    // "session ready" line land in the returned buffer, not in a return
    // value, since nothing here is synchronously known yet when this
    // returns.
    text::Buffer* StartSession(const std::string& agentName);

    // Sends session/prompt for the active session. Returns a short,
    // immediate status string ("Sent." or an explanation of why not) for
    // the caller's own echo-area message; the actual reply streams into the
    // output buffer asynchronously via session/update, not through this
    // return value.
    std::string SendPrompt(const std::string& text);

    // Best-effort session/close, then tears the session down regardless
    // (DapManager::StopSession's own "must not depend on the agent
    // answering" shape) -- returns a short status string.
    std::string StopSession();

    // ACP chat-feel backlog: sends "session/cancel" for the in-flight
    // session/prompt request, if any -- Escape's interrupt affordance in
    // AcpPanel, distinct from StopSession (which tears the whole session
    // down) and CancelPermissionPrompt (which answers a pending permission
    // request, not a prompt). A no-op returning false if nothing is
    // currently in flight or no session is active. This doesn't locally
    // fabricate a "cancelled" transcript entry -- the agent is expected to
    // resolve the pending session/prompt request with stopReason
    // "cancelled" shortly after, which SendPrompt's own response handler
    // already surfaces (any stopReason other than "end_turn"/"end" becomes
    // a SessionEvent), keeping whatever partial reply already streamed in.
    bool CancelPrompt();

    // True from SendPrompt until its session/prompt response arrives (by
    // completion or cancellation) -- what AcpPanel's Escape handler gates
    // on to choose interrupt vs. close-panel, and what drives the "ACP"
    // mode-line spinner (BackgroundActivity.h) between sending a prompt and
    // the first token, closing the gap where SessionState stays Active with
    // no other on-screen change.
    [[nodiscard]] bool PromptInFlight() const;

    // ACP checkpoint/rewind follow-up. One user turn's worth of file-edit
    // bookkeeping, captured across the whole session/prompt round trip
    // (SendPrompt -> the fs/write_text_file calls it provokes -> the
    // session/prompt response). Deliberately independent of Editor/
    // ProjectUndo.h's own transaction stack -- that one is shared with plain
    // per-buffer undo/redo (C-_/M-/) and LSP multi-file edits, and folding
    // ACP turns into the same LIFO would make "jump to an arbitrary earlier
    // turn" (not just "one step back from the top") impossible to express
    // cleanly. Instead this keeps its own ordered history, walked directly
    // via Buffer::CurrentUndoSequence()/TryJumpToUndoSequence() -- the same
    // primitives ProjectUndoManager itself is built on.
    struct CheckpointFileRecord {
        std::filesystem::path path;
        std::size_t           beforeSequence = 0;
        std::size_t           afterSequence  = 0;
    };
    struct Checkpoint {
        std::size_t                           transcriptIndex = 0; // index of this turn's own UserMessage entry
        std::string                           promptPreview;       // single-line, length-capped prompt text
        std::chrono::system_clock::time_point timestamp;
        std::vector<CheckpointFileRecord>     fileRecords;    // buffers open in ned during this turn -- fully rewindable
        std::vector<std::filesystem::path>    untrackedPaths; // written by the agent but never open in ned -- only a
                                                              // Backup.h version (if any) can recover these
    };
    [[nodiscard]] std::size_t CheckpointCount() const;
    // index 0 = oldest turn. An out-of-range index is a caller-contract
    // violation (AcpPanel's own picker only ever iterates
    // 0..CheckpointCount()-1) -- throws std::out_of_range, matching
    // std::vector::at.
    [[nodiscard]] const Checkpoint& CheckpointAt(std::size_t index) const;

    struct RewindOutcome {
        std::string              description; // the target turn's own prompt preview, for a confirmation message
        std::size_t              turnsRewound = 0;
        std::vector<std::string> revertedFiles;
        std::vector<std::string> divergedFiles;  // edited again since this turn -- left untouched
        std::vector<std::string> untrackedFiles; // never tracked (closed, or never open in ned) -- unaffected
    };
    // Rewinds to right before the turn at checkpoints index `index`: every
    // checkpoint from the most recent back through (and including) `index`
    // is walked newest-first, each buffer still open and undiverged since
    // (CurrentUndoSequence() still equals what that turn's own afterSequence
    // recorded) is jumped back to beforeSequence; the transcript is then
    // truncated back to that turn's own UserMessage entry (removed along
    // with everything after it -- linear history, not a tree: this doesn't
    // attempt to keep the discarded turns re-reachable, matching Claude
    // Code's own rewind model), and every checkpoint from `index` onward is
    // dropped. A SessionEvent summarizing the outcome is pushed as the new
    // transcript tail. Deliberately does not touch the agent's own
    // server-side memory of the discarded turns -- ACP has no
    // session/truncate-equivalent call yet (see ROADMAP.md's "AI-assisted
    // editing (ACP) gaps" entry); the agent may still recall what was
    // rewound on its next reply. A no-op (default-constructed outcome) if
    // `index` is out of range.
    RewindOutcome RewindTo(std::size_t index);

    // subprocess-hang-protection follow-up. A no-op if no session is active;
    // otherwise forwards to the live client_'s own ExpireStaleRequests. See
    // LspManager::ExpireStaleRequests's identical wiring/reasoning -- meant
    // to be called from the same periodic background tick.
    //
    // ACP round-1-live-validation follow-up: also a no-op whenever a
    // permission prompt is currently pending (pendingPermissionPrompt_ has a
    // value). A pending permission prompt means *we* -- not the agent, not
    // the transport -- are the reason the outstanding session/prompt hasn't
    // resolved yet, and a human deciding whether to allow a tool call can
    // easily take longer than ProtocolRequestTimeoutMs(). Confirmed live:
    // without this guard, a real permission decision that took a bit over
    // 30s hard-expired the whole prompt out from under the agent, which was
    // correctly waiting on us.
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

    // session/request_permission, exposed for BufferView to render as a
    // numbered-choice prompt (LspCodeActionSelect's own shape) -- Editor/
    // stays UI-free, so this hands out the data and a resolution entry
    // point rather than driving any UI itself, the same boundary
    // DapManager's CurrentStopKeyAndLine()/pending-state exposure already
    // establishes.
    struct PermissionOption {
        std::string optionId;
        std::string name;
        std::string kind; // e.g. "allow_once"/"allow_always"/"reject_once"/"reject_always" -- informational only here
    };
    struct PermissionPrompt {
        std::string                   description;
        std::vector<PermissionOption> options;
    };

    // Invoked on the main thread the moment a session/request_permission
    // request arrives -- unlike every other callback in this class, this
    // one is *not* a "connect after construction, unset is a safe no-op"
    // convenience: if nothing is wired here, the request is left pending
    // forever (no default resolution -- see ResolvePermissionPrompt/
    // CancelPermissionPrompt, both of which WindowManager's wiring is
    // expected to reach from a real keystroke). WindowManager is the
    // intended wirer, forwarding to whichever pane currently has focus,
    // mirroring its own SetOnStopped wiring for DapManager.
    void SetOnPermissionRequest(std::function<void(const PermissionPrompt&)> handler);

    // Answers the pending permission request (if any -- a safe no-op
    // otherwise, e.g. a stale keystroke arriving after the agent already
    // gave up waiting) with {"outcome": "selected", "optionId": optionId}.
    void ResolvePermissionPrompt(const std::string& optionId);
    // Answers the pending permission request (if any) with
    // {"outcome": "cancelled"} -- Escape/C-g's own resolution.
    void CancelPermissionPrompt();
    // Test/introspection seam, mirrors DapManager's own small state
    // queries -- nullopt when nothing is currently pending.
    [[nodiscard]] const std::optional<PermissionPrompt>& PendingPermissionPrompt() const;

    // The session ended for any reason -- agent disconnect/crash, a failed
    // handshake step, or StopSession. reason is short, user-facing text
    // (also appended to the output buffer, so this is purely for a status
    // line elsewhere, e.g. WindowManager forwarding to the shared echo
    // area the same way it does for DapManager::SetOnSessionEnded).
    void SetOnSessionEnded(std::function<void(std::string reason)> handler);

    // Public primarily for tests -- mirrors DapManager::SetClientForTesting
    // exactly: registers an already-constructed AcpClient (typically
    // pipe-backed, no real subprocess) as the session's client without
    // starting the handshake; the next StartSession(name) then runs the
    // real handshake against it instead of spawning.
    AcpClient& SetClientForTesting(std::unique_ptr<AcpClient> client);

  private:
    void          WireClient(AcpClient& client);
    void          HandleSessionUpdate(const Json& params);
    void          EndSession(std::string reason);
    void          AppendToOutputBuffer(std::string_view text);
    text::Buffer& OutputBuffer(const std::string& agentName);

    void PushOrAppendAgentText(TranscriptEntry::Kind kind, std::string_view text);
    void PushOrUpdateToolCall(const Json& update);
    void PushOrReplacePlan(const Json& update);
    void PushTranscriptEntry(TranscriptEntry entry);
    void PushSessionEvent(std::string text);
    void NotifyTranscriptChanged();

    // ACP checkpoint/rewind follow-up. Records/updates pendingCheckpoint_'s
    // entry for `path` -- called from the fs/write_text_file handler with
    // `buffer`'s CurrentUndoSequence() captured immediately before that
    // write was applied. A safe no-op if no turn is currently pending (a
    // tool call arriving outside an in-flight SendPrompt shouldn't happen,
    // but this stays defensive rather than assuming it can't).
    void RecordCheckpointFileEdit(text::Buffer& buffer, const std::filesystem::path& path, std::size_t beforeSequence);
    // Moves pendingCheckpoint_ (if any) onto checkpoints_ -- called once a
    // turn's own session/prompt response arrives (success, error, or a
    // session tearing down mid-turn), never mid-turn.
    void FinalizePendingCheckpoint();

    text::BufferList&   bufferList_;
    ned::ui::EventLoop& eventLoop_;

    std::unique_ptr<AcpClient> client_;
    std::string                agentName_;
    std::string                sessionId_;
    SessionState               state_          = SessionState::Inactive;
    bool                       promptInFlight_ = false;

    std::optional<PermissionPrompt> pendingPermissionPrompt_;
    RespondFn                       pendingPermissionRespond_;

    std::function<void(const PermissionPrompt&)> onPermissionRequest_;
    std::function<void(std::string)>             onSessionEnded_;

    std::vector<TranscriptEntry> transcript_;
    std::size_t                  transcriptGeneration_ = 0;
    std::optional<std::size_t>   livePlanEntryIndex_; // index into transcript_, reset on EndSession

    // ACP checkpoint/rewind follow-up. pendingCheckpoint_ accumulates the
    // in-flight turn's file edits (started in SendPrompt, finalized by
    // FinalizePendingCheckpoint into checkpoints_ once that turn's
    // session/prompt response arrives); checkpoints_ itself persists across
    // StartSession/EndSession, matching transcript_'s own "never cleared"
    // lifetime, so a rewind can still reach a turn from an earlier session
    // in this same process.
    std::vector<Checkpoint>      checkpoints_;
    std::optional<Checkpoint>    pendingCheckpoint_;
    std::function<void()>        onTranscriptChanged_;
    // ACP chat-feel round 2: coalesces the UI-facing onTranscriptChanged_
    // callback while a reply streams in token-by-token -- transcript_/
    // transcriptGeneration_ above stay synchronously correct on every single
    // chunk either way (nothing here is delayed for *data* consumers), only
    // the repaint-triggering notification is debounced. Confirmed live as
    // visible jitter otherwise: AcpPanel's own transcript rendering
    // word-wraps the whole logical line fresh on every Paint(), so a
    // several-times-a-second notification for a several-byte append was
    // reflowing (and therefore visibly reshuffling) the trailing few lines
    // of the panel every single chunk.
    ned::ui::DeadlineTimer agentTextRepaintDebounce_;
};

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_ACPMANAGER_H
