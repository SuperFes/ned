//
// DAP client — slice 1. Owns the (single) running debug session and the
// process-wide breakpoint store — analogous to Lsp/LspManager.h, but
// deliberately one session at a time rather than a per-language map:
// debugging is a modal activity in a way language servers aren't, and
// nothing in slice 1's scope needs two adapters live at once (see
// ROADMAP.md's DAP entry).
//
// Constructed once, alongside lspManager/taskRunner, and passed by
// reference the same way. Threading matches the rest of this subsystem:
// every public method runs on the main thread (called from BufferView's
// key handling), and every DapClient callback is already Post-marshaled
// onto the main thread — no mutexes needed, same reasoning LspClient.h
// documents.
//
// Session shape (the DAP handshake, for whoever touches this next):
//   1. spawn adapter, send `initialize` (capabilities exchange);
//   2. on its response, send `launch` with the user's configured,
//      adapter-specific arguments (DapConfig.h);
//   3. the adapter fires the `initialized` EVENT (distinct from the
//      initialize RESPONSE — the protocol's naming, not ours) once it's
//      ready for breakpoints: send `setBreakpoints` per file, then
//      `configurationDone`;
//   4. thereafter `stopped` events (breakpoint hit, pause, ...) alternate
//      with continue/step requests until a `terminated`/`exited` event or
//      the adapter exits.
//
// Public methods return a short, user-facing status string for immediate
// display; async outcomes (hitting a breakpoint, the session ending) arrive
// through SetOnStopped/SetOnSessionEnded, wired once by WindowManager.
//

#ifndef NED_EDITOR_DAP_DAPMANAGER_H
#define NED_EDITOR_DAP_DAPMANAGER_H

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/ProcessTimeouts.h"
#include "UI/EventLoop.h"

#include "DapClient.h"

namespace ned::editor::dap {

class DapManager {
  public:
    explicit DapManager(ned::ui::EventLoop& eventLoop);
    ~DapManager() = default;

    DapManager(const DapManager&)            = delete;
    DapManager& operator=(const DapManager&) = delete;

    // Starting: initialize/launch/configurationDone still in flight.
    // Running: the debuggee is executing. Stopped: paused at a breakpoint/
    // step/pause, with a thread to resume.
    enum class SessionState { Inactive,
                              Starting,
                              Running,
                              Stopped };
    [[nodiscard]] SessionState State() const;

    // Toggles a source-line breakpoint (line is 1-based, matching DAP's own
    // default and the "path:line" convention every results buffer here
    // already uses). Returns true if the breakpoint is now set, false if it
    // was just removed. Kept across sessions (set before any session
    // starts, still there for the next one); pushed to a live adapter
    // immediately via setBreakpoints when a session is active. path is
    // normalized (weakly_canonical) so the same file toggled via different
    // spellings lands in one entry.
    bool ToggleBreakpoint(const std::filesystem::path& path, std::size_t line);

    // Slice 4: one breakpoint's full state. condition/logMessage empty
    // means "none" -- a logMessage present means the breakpoint never
    // actually halts the debuggee (DAP's own logpoint semantics), the
    // adapter just formats and logs it. verified starts optimistic (true)
    // and is corrected by the next setBreakpoints response's own
    // "verified"/"message" fields -- see SendBreakpointsForFile.
    struct Breakpoint {
        std::size_t line = 0;
        std::string condition;
        std::string logMessage;
        bool        verified = true;
    };

    // Slice 4: sets/clears (empty string) the condition or log message on
    // the breakpoint at path:line, creating a plain breakpoint there first
    // if none exists yet (mirrors real Emacs dap-mode: setting a condition
    // implies wanting a breakpoint). Pushes to a live adapter immediately,
    // same as ToggleBreakpoint. Returns a short status string, appending a
    // capability warning (see Capabilities below) when the active
    // adapter's own initialize response said it doesn't support the
    // feature -- informational only, the field is still sent regardless
    // (most adapters honor fields they didn't advertise).
    std::string SetBreakpointCondition(const std::filesystem::path& path, std::size_t line, std::string condition);
    std::string SetBreakpointLogMessage(const std::filesystem::path& path, std::size_t line, std::string logMessage);

    // Sorted breakpoint lines for path (normalized the same way) — empty if
    // none. Backs slice 2's gutter markers; public now for tests.
    [[nodiscard]] std::vector<std::size_t> BreakpointsForFile(const std::filesystem::path& path) const;

    // F5. No session: starts one for language (adapter + launch config both
    // required, see DapConfig.h). Stopped: sends `continue` for the stopped
    // thread. Starting/Running: reports that, changes nothing.
    std::string StartOrContinue(const std::string& language);

    // Requests a pause of the running debuggee (the adapter answers with a
    // `stopped` event, which flows through SetOnStopped like any other).
    std::string Pause();

    // S-F5. Sends a best-effort `disconnect` (terminateDebuggee: true),
    // then tears the session down immediately — the teardown must not
    // depend on a well-behaved adapter answering.
    std::string StopSession();

    // Slice 2: F10/F11/S-F11 (DAP's own `next`/`stepIn`/`stepOut`
    // requests). All require a Stopped session (the thread the last
    // `stopped` event named is the one stepped); the adapter answers with
    // another `stopped` event (reason "step"), which flows through
    // SetOnStopped like any other.
    std::string StepOver();
    std::string StepInto();
    std::string StepOut();

    // Slice 2, for BufferView's gutter marker + execution-line highlight:
    // where the debuggee is currently stopped, as an already-normalized
    // path key (same normalization ToggleBreakpoint applies, so callers
    // compare keys, never re-derive paths) plus the 1-based line. Set when
    // a stop's top frame had a real source location; cleared on any
    // resume (continue/step) and at session end.
    [[nodiscard]] std::optional<std::pair<std::string, std::size_t>> CurrentStopKeyAndLine() const;

    // Slice 2: BreakpointsForFile's cheap sibling for per-frame rendering
    // -- takes an already-normalized key (NormalizePathKey below) so
    // Paint() never pays path canonicalization per frame.
    [[nodiscard]] std::vector<std::size_t> BreakpointLinesForKey(const std::string& key) const;

    // Slice 4: BreakpointLinesForKey's richer sibling -- full Breakpoint
    // state (condition/logMessage/verified) per line, for the gutter's
    // glyph-by-kind/color-by-verified rendering. Sorted by line, same key
    // convention as BreakpointLinesForKey.
    [[nodiscard]] std::vector<Breakpoint> BreakpointsForKey(const std::string& key) const;

    // Public (slice 2) so BufferView can normalize the active buffer's own
    // path once (cached per buffer) and compare keys against
    // CurrentStopKeyAndLine/BreakpointLinesForKey.
    [[nodiscard]] static std::string NormalizePathKey(const std::filesystem::path& path);

    // session-persistence slice 2: the whole store, in its own shape
    // (normalized path key -> sorted 1-based lines), for ProjectSessionData
    // to carry across restarts -- closes the "persisting breakpoints across
    // restarts" v1 cut recorded in ROADMAP.md's DAP entry. Slice 4: returns
    // by value now (a fresh line-only projection of the richer internal
    // Breakpoint store) rather than a direct reference -- condition/
    // logMessage/verified deliberately do NOT round-trip through session
    // persistence (see ROADMAP.md), so this keeps the on-disk shape and
    // every ProjectSession.cpp/WindowManager.cpp caller unchanged.
    [[nodiscard]] std::map<std::string, std::vector<std::size_t>> AllBreakpoints() const;

    // Replaces the store wholesale (keys are trusted to be already
    // normalized -- they round-trip verbatim from AllBreakpoints via the
    // session file). If a session is somehow live, every file whose set
    // changed is pushed to the adapter; in the real startup wiring this
    // runs long before any session can exist, so that path is a
    // robustness guard, not a designed-for flow.
    void RestoreBreakpoints(std::map<std::string, std::vector<std::size_t>> breakpoints);

    // subprocess-hang-protection follow-up. A no-op if no session is active;
    // otherwise forwards to the live client_'s own ExpireStaleRequests. See
    // LspManager::ExpireStaleRequests's identical wiring/reasoning -- meant
    // to be called from the same periodic background tick.
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

    // Slice 3: the inspection requests backing the *debug* buffer. Each
    // callback runs on the main thread with parsed results ([] on any
    // failure -- no session, adapter error, malformed response), the same
    // graceful-empty convention LspManager's own Request* callbacks use.
    struct StackFrame {
        int                                  id = 0; // the adapter's own frame id, fed back to RequestScopes
        std::string                          name;
        std::optional<std::filesystem::path> path;
        std::size_t                          line = 0; // 1-based, valid only when path is set
    };
    void RequestStackTrace(std::function<void(std::vector<StackFrame>)> callback);

    struct Scope {
        std::string name;
        int         variablesReference = 0; // fed back to RequestVariables
    };
    void RequestScopes(int frameId, std::function<void(std::vector<Scope>)> callback);

    struct Variable {
        std::string name;
        std::string value;
        std::string type;                   // empty if the adapter sent none
        int         variablesReference = 0; // > 0 means expandable (a composite) via RequestVariables
    };
    void RequestVariables(int variablesReference, std::function<void(std::vector<Variable>)> callback);

    // Slice 3: one-shot expression evaluation, against the stopped top
    // frame when there is one. callback(success, text) -- text is the
    // result or a short error, either way ready for the echo area. Slice 4
    // added the context parameter -- DAP's own evaluate-context enum;
    // "repl" (default) is dap-evaluate's/the debug console's own manual
    // evaluation, "watch" is what ShowDebugInfo's watch-expression fan-out
    // passes (some adapters suppress side effects only for "watch").
    void Evaluate(const std::string& expression, std::function<void(bool, std::string)> callback,
                   std::string context = "repl");

    // Slice 4: watch expressions -- a plain ordered list, re-evaluated
    // (via Evaluate, "watch" context) by BufferView::ShowDebugInfo every
    // time the *debug* buffer is rebuilt. Session-lifetime only, not
    // persisted (see ROADMAP.md's DAP entry).
    void                            AddWatch(std::string expression);
    void                            RemoveWatchAt(std::size_t index);
    [[nodiscard]] const std::vector<std::string>& Watches() const;

    // Slice 4: the thread picker. RequestThreads lists every thread the
    // adapter currently reports (graceful [] on no session/adapter error,
    // same convention as RequestStackTrace). SelectThread changes which
    // thread every subsequent inspection/step/continue request targets
    // (CurrentThreadId(), used internally) and refreshes the top-frame id
    // Evaluate scopes to -- callback(success) once that refresh lands.
    struct Thread {
        int         id = 0;
        std::string name;
    };
    void RequestThreads(std::function<void(std::vector<Thread>)> callback);
    void SelectThread(int threadId, std::function<void(bool)> callback);

    // Slice 4: edits a variable in place via DAP's setVariable request.
    // variablesReference is the *container's* reference (the scope or
    // parent composite variable the edited one was fetched from via
    // RequestVariables) -- not the variable's own. callback(success,
    // newValue, newType, newVariablesReference, errorMessage) -- on
    // failure only errorMessage is meaningful, the rest default-empty/0.
    struct SetVariableResult {
        bool        success = false;
        std::string value;
        std::string type;
        int         variablesReference = 0;
        std::string errorMessage;
    };
    void SetVariable(int variablesReference, const std::string& name, const std::string& value,
                      std::function<void(SetVariableResult)> callback);

    // Where the debuggee stopped. path/line are set when the adapter's own
    // top stack frame had a real source location (fetched via a stackTrace
    // request on every stop), and unset otherwise (e.g. stopped inside
    // sourceless library code).
    struct StoppedInfo {
        std::string                          reason; // "breakpoint", "step", "pause", ...
        std::optional<std::filesystem::path> path;
        std::size_t                          line = 0; // 1-based, valid only when path is set
    };
    void SetOnStopped(std::function<void(const StoppedInfo&)> handler);

    // The session ended for any reason — terminated/exited event, adapter
    // crash/EOF, a failed handshake step, or StopSession. reason is short,
    // user-facing text.
    void SetOnSessionEnded(std::function<void(std::string reason)> handler);

    // Public primarily for tests — mirrors LspManager::SetClientForTesting
    // exactly (see that method's doc comment): registers an already-
    // constructed DapClient (typically pipe-backed, no real subprocess) as
    // the session's client without starting the handshake; the next
    // StartOrContinue(language) then runs the real handshake against it
    // instead of spawning. Returns the client for the test to keep driving
    // via DispatchFrame.
    DapClient& SetClientForTesting(std::unique_ptr<DapClient> client);

  private:
    void SendLaunch();
    void SendBreakpointsForFile(const std::string& pathKey);
    void HandleInitializedEvent();
    void HandleStoppedEvent(const Json& body);
    // The shared body of StepOver/StepInto/StepOut -- command is the DAP
    // request name, label the human-readable status verb.
    std::string SendStep(const std::string& command, const std::string& label);
    // Marks the session running again: state, stop location, and frame id
    // all cleared together (continue and every step share this).
    void MarkResumed();
    // Tears down the session and reports reason via onSessionEnded_.
    // lsp-use-after-free follow-up: destroys client_ directly now -- see
    // this method's own .cpp comment for why an earlier "move into a
    // retired_ vector instead, drain later" version of this comment no
    // longer applies (the real fix lives in DapClient itself now).
    void EndSession(std::string reason);
    void WireClient(DapClient& client);
    // Slice 4: the thread every inspection/step/continue request targets --
    // the explicitly focused one (SelectThread) if set, else whichever
    // thread the last `stopped` event named.
    [[nodiscard]] int CurrentThreadId() const;

    ned::ui::EventLoop& eventLoop_;

    std::unique_ptr<DapClient> client_;
    std::string                language_;
    SessionState                            state_           = SessionState::Inactive;
    int                                     stoppedThreadId_ = 0;
    // Where the debuggee is stopped (slice 2) -- see CurrentStopKeyAndLine.
    std::optional<std::pair<std::string, std::size_t>> currentStop_;
    // The stopped top frame's own adapter-assigned id (slice 3) -- what
    // Evaluate scopes an expression to. Cleared alongside currentStop_.
    std::optional<int> stoppedFrameId_;
    // Slice 4: explicit thread focus set via SelectThread -- see
    // CurrentThreadId(). Cleared alongside stoppedFrameId_/currentStop_ (a
    // resumed debuggee has no meaningful "focused thread" until it stops
    // again and HandleStoppedEvent re-seeds it from the new stoppedThreadId_).
    std::optional<int> focusedThreadId_;

    std::map<std::string, std::vector<Breakpoint>> breakpoints_; // normalized path -> sorted-by-line breakpoints

    // Slice 4: adapter capabilities from the last `initialize` response --
    // reset at the top of every new session. Used only to append a soft,
    // informational warning to a status string; never blocks an action
    // (adapters routinely under-advertise but still honor a field).
    struct Capabilities {
        bool conditionalBreakpoints = false;
        bool logPoints              = false;
        bool setVariable            = false;
    };
    Capabilities capabilities_;

    std::vector<std::string> watches_; // slice 4, session-lifetime only -- see AddWatch/Watches

    std::function<void(const StoppedInfo&)> onStopped_;
    std::function<void(std::string)>        onSessionEnded_;
};

} // namespace ned::editor::dap

#endif // NED_EDITOR_DAP_DAPMANAGER_H
