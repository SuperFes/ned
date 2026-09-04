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
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
        // DAP round 3: DAP's own `hitCondition` -- an adapter-evaluated
        // expression ("> 5", "== 10", adapter-specific grammar) against the
        // running hit count; the breakpoint only actually stops once it's
        // satisfied. Same "empty means none" convention as condition/
        // logMessage.
        std::string hitCondition;
        bool        verified = true;
        // DAP round 4: the adapter's own snapped location from the last
        // setBreakpoints response ("line" per entry), when it differs from
        // the requested `line` above (e.g. a breakpoint toggled on a
        // comment/blank line, moved to the next real statement). 0 means
        // "not yet known, or the adapter didn't move it" -- a safe sentinel
        // since DAP lines are always 1-based. Editing operations (toggle/
        // condition/logMessage/hitCondition) stay addressed by the
        // requested `line`, matching where the user's cursor was; only the
        // gutter's display row follows actualLine when set.
        std::size_t actualLine = 0;
    };

    // session-persistence round 2: one breakpoint's persistable state --
    // condition/logMessage/hitCondition now round-trip across a restart
    // (closing the "session storage deliberately stayed the old line-only
    // shape" gap recorded in ROADMAP.md). verified/actualLine deliberately
    // stay OUT of this shape: both are live-adapter-derived facts that only
    // mean anything relative to a session that's actually running one of
    // this breakpoint's setBreakpoints requests, so every restored
    // breakpoint starts fresh (verified=true, actualLine=0) exactly like a
    // newly-toggled one -- see RestoreBreakpoints.
    struct PersistedBreakpoint {
        std::size_t line = 0;
        std::string condition;
        std::string logMessage;
        std::string hitCondition;
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
    // DAP round 3: SetBreakpointCondition's exact sibling for hitCondition.
    std::string SetBreakpointHitCondition(const std::filesystem::path& path, std::size_t line, std::string hitCondition);

    // Sorted breakpoint lines for path (normalized the same way) — empty if
    // none. Backs slice 2's gutter markers; public now for tests.
    [[nodiscard]] std::vector<std::size_t> BreakpointsForFile(const std::filesystem::path& path) const;

    // DAP round 3: function breakpoints -- a separate store from the
    // line-keyed one above, DAP's own `setFunctionBreakpoints` request.
    // Toggle returns true if now set, false if just removed (ToggleBreakpoint's
    // own shape); pushed to a live adapter immediately, same as
    // ToggleBreakpoint. Kept sorted+deduped. No gutter representation (not
    // tied to a line) -- status-string feedback only.
    bool                                          ToggleFunctionBreakpoint(std::string name);
    [[nodiscard]] const std::vector<std::string>& FunctionBreakpoints() const;

    // DAP round 3: exception breakpoints -- DAP's `setExceptionBreakpoints`
    // request. The adapter advertises available filters on its `initialize`
    // response (id/label/whether it's on by default); enabled ones are
    // whichever the adapter marked default until SetExceptionBreakpointFilters
    // overrides the set, both reset fresh at the start of every new session
    // -- never persisted, unlike breakpoint conditions/watches (session-
    // persistence round 2): a filter id is only meaningful relative to
    // whichever adapter advertised it, so there's nothing stable to restore
    // against before the next session's own initialize response arrives.
    struct ExceptionFilter {
        std::string id;
        std::string label;
        bool        defaultEnabled = false;
    };
    [[nodiscard]] const std::vector<ExceptionFilter>& AvailableExceptionFilters() const;
    [[nodiscard]] const std::set<std::string>&        EnabledExceptionFilters() const;
    // Replaces the enabled set wholesale and pushes to a live adapter
    // immediately (a no-op send target if no session is active -- the next
    // session re-seeds from its own defaults regardless, this isn't
    // persisted).
    void SetExceptionBreakpointFilters(std::set<std::string> ids);

    // F5. No session: starts one for language (adapter + launch config both
    // required, see DapConfig.h). Stopped: sends `continue` for the stopped
    // thread. Starting/Running: reports that, changes nothing.
    std::string StartOrContinue(const std::string& language);

    // DAP round 3: attach instead of launch -- requires an attach
    // configuration (ned/set-dap-attach) rather than a launch one, and
    // (unlike StartOrContinue) is start-only: no continue/resume semantics,
    // since "attach" isn't a thing you resume into. Inactive-only; any
    // other state reports "Debug session already running." unchanged.
    std::string Attach(const std::string& language);

    // Requests a pause of the running debuggee (the adapter answers with a
    // `stopped` event, which flows through SetOnStopped like any other).
    std::string Pause();

    // S-F5. Sends a best-effort `disconnect` (terminateDebuggee: true for a
    // launched session, false for an attached one -- DAP round 3: ned never
    // kills a process it didn't start), then tears the session down
    // immediately — the teardown must not depend on a well-behaved adapter
    // answering.
    std::string StopSession();

    // Slice 2: F10/F11/S-F11 (DAP's own `next`/`stepIn`/`stepOut`
    // requests). All require a Stopped session (the thread the last
    // `stopped` event named is the one stepped); the adapter answers with
    // another `stopped` event (reason "step"), which flows through
    // SetOnStopped like any other.
    std::string StepOver();
    std::string StepInto();
    std::string StepOut();

    // Debugging wishlist: reverse debugging -- DAP's own `reverseContinue`/
    // `stepBack` requests, both gated by the single `supportsStepBack`
    // capability the adapter advertises on `initialize` (see Capabilities
    // below). Same Stopped-only gating and "landing spot arrives via the
    // next `stopped` event" shape as StepOver/Into/Out; sent regardless of
    // the advertised capability (an adapter may honor it without
    // advertising it) with a soft warning appended when it wasn't
    // advertised, RestartFrame's own convention. Only meaningful against an
    // adapter that itself replays a recording (e.g. rr) -- most adapters
    // simply never advertise support and these become no-ops.
    std::string ReverseContinue();
    std::string StepBack();

    // DAP round 4: DAP's own `restartFrame` request -- reruns the stopped
    // thread from the top of the named frame instead of stepping through it.
    // Same shape as StepOver/StepInto/StepOut (Stopped-only, the new
    // position arrives via the next `stopped` event) but takes an explicit
    // frameId rather than always targeting CurrentThreadId()'s own top frame
    // -- BufferView resolves frameId from a `[frame:N]` marker on the
    // *debug* buffer's stack line at point (ShowDebugInfo), the same
    // in-text-marker convention FormatDebugVariableLine's `[ref:N]` uses.
    std::string RestartFrame(int frameId);

    // Debugging wishlist (gf/GDBFrontend audit): run-to-cursor -- a
    // temporary breakpoint at path:line, continued past immediately, and
    // cleared again the moment the debuggee next stops for any reason
    // (only one continue was issued, so that next stop is deterministically
    // "wherever this landed" -- gf's/vim's own run-to-cursor semantics). If
    // a real breakpoint already exists at that line, it's left in place
    // untouched -- nothing temporary to clean up. Stopped-only, same gating
    // as StepOver/Into/Out: a debuggee that isn't paused has nothing
    // meaningful to run *to* yet.
    std::string RunToCursor(const std::filesystem::path& path, std::size_t line);

    // Debugging wishlist: jump-to-line (gf's Shift+Click "skip to line") --
    // moves the stopped thread's execution point directly to path:line
    // without running through the skipped code, via DAP's own two-request
    // dance: `gotoTargets` asks the adapter which jump target ids exist at
    // that source line (an arbitrary line isn't necessarily a valid jump
    // target -- the adapter answers with whatever it can actually land on,
    // typically the nearest statement), then `goto` jumps the thread to the
    // first one. Stopped-only, same gating as StepOver/Into/Out. Unlike
    // those, no DapManager state changes here even on success -- the new
    // position arrives the same way a step's does, via the following
    // `stopped` event (reason "goto") flowing through HandleStoppedEvent.
    // callback(success, message) once the whole exchange lands (or fails at
    // either step) -- message is a short, user-facing status either way.
    void JumpToLine(const std::filesystem::path& path, std::size_t line, std::function<void(bool, std::string)> callback);

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
    // (normalized path key -> sorted-by-line PersistedBreakpoints), for
    // ProjectSessionData to carry across restarts -- closes the "persisting
    // breakpoints across restarts" v1 cut recorded in ROADMAP.md's DAP
    // entry. session-persistence round 2: returns PersistedBreakpoint (line
    // + condition/logMessage/hitCondition) rather than a bare line, closing
    // the follow-up gap the same ROADMAP entry recorded -- verified/
    // actualLine still don't round-trip (see PersistedBreakpoint's own doc
    // comment).
    [[nodiscard]] std::map<std::string, std::vector<PersistedBreakpoint>> AllBreakpoints() const;

    // Replaces the store wholesale (keys are trusted to be already
    // normalized -- they round-trip verbatim from AllBreakpoints via the
    // session file). Every restored breakpoint starts verified=true/
    // actualLine=0, same as a fresh ToggleBreakpoint (see
    // PersistedBreakpoint). If a session is somehow live, every file whose
    // set changed is pushed to the adapter; in the real startup wiring this
    // runs long before any session can exist, so that path is a
    // robustness guard, not a designed-for flow.
    void RestoreBreakpoints(std::map<std::string, std::vector<PersistedBreakpoint>> breakpoints);

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
        // DAP round 5 (disassembly): the frame's own instruction pointer, as
        // an opaque adapter-owned address string -- empty when the adapter
        // didn't send one (older adapters, or a frame with no real machine
        // address). Fed straight into RequestDisassembly, never parsed.
        std::string instructionPointerReference;
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
        // DAP round 5 (memory view): an opaque adapter-owned address string
        // for this variable's storage, empty when the adapter didn't send
        // one (most variables -- only pointer/array-shaped ones typically
        // carry one). Fed straight into RequestMemory, never parsed.
        std::string memoryReference;
    };
    // Debugging wishlist: hex is DAP's own `format: {hex: true}` request
    // argument -- a display-only hint (the adapter renders the numeric
    // `value` field in hex instead of decimal; nothing else about the
    // variable changes). Defaults false, matching every existing call site's
    // behavior unchanged.
    void RequestVariables(int variablesReference, std::function<void(std::vector<Variable>)> callback, bool hex = false);

    // DAP round 5: one disassembled instruction, backing dap-show-disassembly.
    struct DisassembledInstruction {
        std::string                          address;          // adapter's own opaque address string
        std::string                          instructionBytes; // may be empty -- adapter-optional
        std::string                          instruction;
        std::optional<std::filesystem::path> path;
        std::size_t                          line = 0; // 1-based, valid only when path is set
    };
    // Sends DAP's `disassemble` request against memoryReference (typically a
    // StackFrame's own instructionPointerReference). instructionOffset may
    // be negative (instructions before memoryReference) -- the adapter's own
    // convention for windowing around an address rather than only forward
    // from it. Graceful [] on no session/adapter error/empty memoryReference,
    // same convention as every other Request* method.
    void RequestDisassembly(const std::string& memoryReference, long instructionOffset, int instructionCount,
                            std::function<void(std::vector<DisassembledInstruction>)> callback);

    // DAP round 5: one `readMemory` response, backing dap-show-memory-at-point.
    struct MemoryBlock {
        std::string               address;
        std::vector<std::uint8_t> data;
        std::size_t               unreadableBytes = 0;
    };
    // Sends DAP's `readMemory` request against memoryReference (typically a
    // Variable's own memoryReference). callback(success, block) -- graceful
    // {false, {}} on no session/adapter error/empty memoryReference/capability
    // absent, same convention as SetVariable's own success flag.
    void RequestMemory(const std::string& memoryReference, long offset, std::size_t count,
                       std::function<void(bool success, MemoryBlock)> callback);

    // Slice 3: one-shot expression evaluation, against the stopped top
    // frame when there is one. callback(success, text) -- text is the
    // result or a short error, either way ready for the echo area. Slice 4
    // added the context parameter -- DAP's own evaluate-context enum;
    // "repl" (default) is dap-evaluate's/the debug console's own manual
    // evaluation, "watch" is what ShowDebugInfo's watch-expression fan-out
    // passes (some adapters suppress side effects only for "watch").
    // Debugging wishlist: hex is RequestVariables's own `format: {hex: true}`
    // hint, applied to evaluate's `result` field the same way. Defaults
    // false, matching every existing call site's behavior unchanged.
    void Evaluate(const std::string& expression, std::function<void(bool, std::string)> callback,
                  std::string context = "repl", bool hex = false);

    // Debugging wishlist: array-value graph -- Evaluate's callback shape
    // (bool, std::string) discards the response's own variablesReference,
    // which BufferView::ToggleWatchGraphAtPoint needs to tell "a plain
    // scalar" from "an expandable value" (a numeric array, graphable as a
    // bar chart via RequestVariables) apart. A separate, narrow entry point
    // rather than widening Evaluate's signature across its five existing
    // call sites, none of which need this.
    struct EvaluateResult {
        bool        success = false;
        std::string text;
        int         variablesReference = 0; // > 0 means expandable, Variable's own convention
    };
    void EvaluateWithReference(const std::string& expression, std::function<void(EvaluateResult)> callback,
                               std::string context = "watch");

    // Slice 4: watch expressions -- a plain ordered list, re-evaluated
    // (via Evaluate, "watch" context) by BufferView::ShowDebugInfo every
    // time the *debug* buffer is rebuilt. Kept across sessions the same way
    // breakpoints_ is (session-persistence round 2 -- closes the "watches
    // deliberately not persisted" gap ROADMAP.md recorded).
    void                                          AddWatch(std::string expression);
    void                                          RemoveWatchAt(std::size_t index);
    [[nodiscard]] const std::vector<std::string>& Watches() const;
    // session-persistence round 2: replaces the watch list wholesale, same
    // "keys/entries trusted, called before any session can exist" contract
    // as RestoreBreakpoints. No live re-evaluation happens here -- the next
    // ShowDebugInfo rebuild picks the restored list up on its own.
    void RestoreWatches(std::vector<std::string> watches);

    // Debugging wishlist: watch-history sparkline -- one numeric value per
    // stop where the watch's expression evaluated to something
    // Editor/Sparkline.h's TryParseNumeric accepts (see RefreshWatchHistory,
    // called automatically from HandleStoppedEvent); a failed or
    // non-numeric evaluation is silently skipped, not recorded as a gap.
    // Indices parallel Watches() exactly (kept in sync by AddWatch/
    // RemoveWatchAt/RestoreWatches); an out-of-range index returns a shared
    // empty vector rather than throwing. Never persisted -- a fresh
    // session's values aren't comparable to a prior run's, so this resets
    // (to one empty history per current watch) at the start of every new
    // session, same as capabilities_.
    [[nodiscard]] const std::vector<double>& WatchHistoryAt(std::size_t index) const;

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
    // Debugging wishlist: a public read-only echo of the private
    // CurrentThreadId() below, for UI/DapThreadsPanel.h's own "mark the
    // current thread's row" need -- every other consumer resolves it only
    // implicitly, via RequestThreads()/SelectThread()'s own internal use.
    [[nodiscard]] int FocusedThreadId() const {
        return CurrentThreadId();
    }

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
    // DAP round 3: the shared body of StartOrContinue's "no session yet"
    // branch and Attach -- spawns/reuses client_, sends `initialize`,
    // parses capabilities, and on success sends `launch` or `attach`
    // (SendLaunchOrAttach, per attach) once the response lands. Returns the
    // same short status string both public entry points hand back.
    std::string BeginSession(const std::string& language, bool attach);
    // Renamed from the original slice-1 SendLaunch: reads isAttach_ to send
    // `launch` (DapLaunchConfig) or `attach` (DapAttachConfig).
    void SendLaunchOrAttach();
    void SendBreakpointsForFile(const std::string& pathKey);
    // DAP round 3: setFunctionBreakpoints/setExceptionBreakpoints --
    // SendBreakpointsForFile's siblings for the two non-line-keyed stores.
    void SendFunctionBreakpoints();
    void SendExceptionBreakpoints();
    void HandleInitializedEvent();
    void HandleStoppedEvent(const Json& body);
    // Debugging wishlist: watch-history sparkline -- fans out one Evaluate
    // per watch (context "watch", ShowDebugInfo's own fan-out convention),
    // called from HandleStoppedEvent's stackTrace response once
    // stoppedFrameId_ is set. A non-numeric or failed evaluation is
    // silently skipped -- see WatchHistoryAt's own doc comment.
    void RefreshWatchHistory();
    // RunToCursor's own cleanup: erases the pending temporary breakpoint (if
    // any) from the store, pushing the change to a live adapter when
    // pushToAdapter is set (HandleStoppedEvent's case -- the session is
    // still alive) but not when it isn't (EndSession's case -- client_ is
    // about to be torn down, nothing to push to).
    void ClearPendingRunToCursor(bool pushToAdapter);
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
    SessionState               state_           = SessionState::Inactive;
    int                        stoppedThreadId_ = 0;
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

    // Run-to-cursor's own temporary breakpoint, when RunToCursor had to
    // create one (normalized key, line) -- unset when the current/last
    // run-to-cursor landed on an already-existing breakpoint, since there's
    // nothing temporary to clear afterward. See RunToCursor/ClearPendingRunToCursor.
    std::optional<std::pair<std::string, std::size_t>> pendingRunToCursor_;

    // DAP round 3: sorted+deduped function-breakpoint names -- see
    // ToggleFunctionBreakpoint.
    std::vector<std::string> functionBreakpoints_;

    // DAP round 3: the adapter's advertised exception filters and which are
    // currently enabled -- see AvailableExceptionFilters/
    // EnabledExceptionFilters/SetExceptionBreakpointFilters. Both reset in
    // EndSession, re-seeded from the new session's own initialize response
    // (never persisted -- adapter-specific filter ids, nothing stable to
    // restore against before the next initialize response, unlike
    // watches_ below).
    std::vector<ExceptionFilter> exceptionFilters_;
    std::set<std::string>        enabledExceptionFilters_;

    // DAP round 3: true for a session started via Attach, false for
    // StartOrContinue's own launch path -- read by SendLaunchOrAttach (which
    // config/request name to use) and StopSession (terminateDebuggee).
    bool isAttach_ = false;

    // Slice 4: adapter capabilities from the last `initialize` response --
    // reset at the top of every new session. Used only to append a soft,
    // informational warning to a status string; never blocks an action
    // (adapters routinely under-advertise but still honor a field).
    struct Capabilities {
        bool conditionalBreakpoints = false;
        bool logPoints              = false;
        bool setVariable            = false;
        // DAP round 3.
        bool hitConditionalBreakpoints = false;
        bool functionBreakpoints       = false;
        // DAP round 4.
        bool restartFrame = false;
        // DAP round 5.
        bool disassemble = false;
        bool readMemory  = false;
        // Debugging wishlist: jump-to-line -- see JumpToLine.
        bool gotoTargets = false;
        // Debugging wishlist: reverse debugging -- DAP's single
        // `supportsStepBack` capability covers both `stepBack` and
        // `reverseContinue` (see ReverseContinue/StepBack).
        bool stepBack = false;
    };
    Capabilities capabilities_;

    std::vector<std::string> watches_; // slice 4; persisted across restarts (round 2) -- see AddWatch/Watches/RestoreWatches
    // Debugging wishlist: watch-history sparkline -- parallel to watches_
    // (same index, kept in sync by AddWatch/RemoveWatchAt/RestoreWatches),
    // never persisted -- see WatchHistoryAt/RefreshWatchHistory.
    std::vector<std::vector<double>> watchHistory_;

    std::function<void(const StoppedInfo&)> onStopped_;
    std::function<void(std::string)>        onSessionEnded_;
};

} // namespace ned::editor::dap

#endif // NED_EDITOR_DAP_DAPMANAGER_H
