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

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
    // Tears down the session and reports reason via onSessionEnded_. The
    // client is moved into retired_, not destroyed in place — EndSession is
    // reached from inside the client's own Post-marshaled callbacks, and
    // destroying the object whose callback is still on the stack would be
    // use-after-free; retired_ drains at the next session start instead.
    void EndSession(std::string reason);
    void WireClient(DapClient& client);

    [[nodiscard]] static std::string NormalizePathKey(const std::filesystem::path& path);

    ned::ui::EventLoop& eventLoop_;

    std::unique_ptr<DapClient>              client_;
    std::vector<std::unique_ptr<DapClient>> retired_; // see EndSession
    std::string                             language_;
    SessionState                            state_           = SessionState::Inactive;
    int                                     stoppedThreadId_ = 0;

    std::map<std::string, std::vector<std::size_t>> breakpoints_; // normalized path -> sorted 1-based lines

    std::function<void(const StoppedInfo&)> onStopped_;
    std::function<void(std::string)>        onSessionEnded_;
};

} // namespace ned::editor::dap

#endif // NED_EDITOR_DAP_DAPMANAGER_H
