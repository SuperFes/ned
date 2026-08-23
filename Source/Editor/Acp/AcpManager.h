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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
    ~AcpManager() = default;

    AcpManager(const AcpManager&)            = delete;
    AcpManager& operator=(const AcpManager&) = delete;

    enum class SessionState { Inactive,
                              Starting,
                              Active };
    [[nodiscard]] SessionState State() const;

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

    text::BufferList&   bufferList_;
    ned::ui::EventLoop& eventLoop_;

    std::unique_ptr<AcpClient>              client_;
    std::vector<std::unique_ptr<AcpClient>> retired_; // see DapManager::EndSession's identical reasoning -- never destroyed mid-callback
    std::string                             agentName_;
    std::string                             sessionId_;
    SessionState                            state_ = SessionState::Inactive;

    std::optional<PermissionPrompt> pendingPermissionPrompt_;
    RespondFn                       pendingPermissionRespond_;

    std::function<void(const PermissionPrompt&)> onPermissionRequest_;
    std::function<void(std::string)>             onSessionEnded_;
};

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_ACPMANAGER_H
