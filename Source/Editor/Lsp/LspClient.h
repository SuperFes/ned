//
// LSP client follow-up. One JSON-RPC 2.0 connection to one running language
// server process (see Transport.h for the raw process/pipe layer this sits
// on top of).
//
// Threading: a background std::jthread runs a blocking read loop
// (Transport::ReadFrame), marshaling each parsed frame onto the main FTXUI
// loop thread via ftxui::ScreenInteractive::Post -- the same
// documented-thread-safe mechanism WindowManager::StartAutoSaveTimer already
// established, just driven by a blocking read loop instead of a timer. Every
// public method here, and DispatchFrame (invoked only via that Post
// callback), therefore only ever runs on the main thread -- pending_ and
// notificationHandlers_ deliberately have no mutex guarding them, unlike
// most process-wide state elsewhere in this codebase, because they're never
// actually touched from two threads at once: the background thread only
// ever calls Transport::ReadFrame (which shares no mutable state with these
// maps) and hands the *result* across via Post, never touching them itself.
//
// Lifetime: LspClient (and whatever owns it, e.g. LspManager) must only be
// destroyed after ftxui::ScreenInteractive::Loop() has returned -- same
// requirement WindowManager's own Post-based timer already has. A frame
// posted but not yet processed when destruction begins is simply abandoned,
// same as any other leftover Post callback at shutdown; this is only safe
// because destruction never races the main thread actually processing that
// queue (Loop() has already stopped doing so by the time destruction can
// happen).
//
// Member declaration order below is load-bearing, not just style: destroying
// transport_ (which happens before readThread_, since C++ destroys members
// in reverse declaration order, and transport_ is declared *after*
// readThread_) is what makes readThread_'s own destructor-driven
// request_stop()+join() actually terminate promptly -- Transport's own
// destructor closes this end's fds and kills+reaps the child process, which
// is what makes the background thread's in-flight, otherwise-unstoppable
// blocking Transport::ReadFrame() call finally return (EOF, once the child's
// stdout pipe has no writers left). A stop_token alone cannot interrupt a
// blocking read() -- this ordering is the actual interruption mechanism, not
// a defensive extra.
//

#ifndef NED_EDITOR_LSP_LSPCLIENT_H
#define NED_EDITOR_LSP_LSPCLIENT_H

#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>
#include <nlohmann/json.hpp>

#include "Transport.h"

namespace ned::editor::lsp {

using Json = nlohmann::json;

// Exactly one of result/error is engaged, matching JSON-RPC 2.0's own
// response shape.
using ResponseCallback    = std::function<void(std::optional<Json> result, std::optional<Json> error)>;
using NotificationHandler = std::function<void(const Json& params)>;

class LspClient {
  public:
    // Spawns argv as a new language server process. screen must outlive this
    // LspClient (see this file's own header comment on lifetime).
    LspClient(std::vector<std::string> argv, ftxui::ScreenInteractive& screen);

    // Takes ownership of an already-open Transport directly -- for tests
    // driving a raw pipe pair with no real subprocess involved.
    LspClient(Transport transport, ftxui::ScreenInteractive& screen);

    ~LspClient() = default; // member destruction order does the real work -- see header comment

    LspClient(const LspClient&)            = delete;
    LspClient& operator=(const LspClient&) = delete;
    // Not movable: the background thread's lambda captures `this` directly.
    LspClient(LspClient&&)            = delete;
    LspClient& operator=(LspClient&&) = delete;

    // Sends a JSON-RPC request with a freshly allocated id. callback runs on
    // the main thread once the matching response arrives; if this LspClient
    // is destroyed first, callback is simply dropped, uninvoked (matches
    // this class's own "abandoned at shutdown" convention -- see header
    // comment).
    void SendRequest(const std::string& method, Json params, ResponseCallback callback);

    void SendNotification(const std::string& method, Json params);

    // Replaces any existing handler for method. Invoked on the main thread
    // for every server-initiated notification with this method name (e.g.
    // "textDocument/publishDiagnostics"). A server-initiated *request*
    // (carries its own "id", expects a response) is not supported in this
    // slice -- no server capability this client currently declares needs
    // one.
    void SetNotificationHandler(std::string method, NotificationHandler handler);

    // Public primarily for tests: the real background read loop always
    // reaches this via screen_.Post (see the header comment above), but
    // FTXUI's own App::Post only ever enqueues onto its task runner --
    // confirmed by reading app.cpp, not assumed -- there's no synchronous
    // fallback the way the *static* PostEventOrExecute helper has, so a
    // test would need a real, running ScreenInteractive::Loop() to ever see
    // a Post-driven call actually happen. Calling this directly instead
    // exercises the exact same correlation/dispatch logic without needing
    // that.
    void DispatchFrame(const std::string& frameText);

  private:
    void StartReadLoop();

    std::jthread readThread_; // declared before transport_ -- see header comment
    Transport    transport_;

    ftxui::ScreenInteractive& screen_;

    int                                                  nextRequestId_ = 1;
    std::unordered_map<int, ResponseCallback>            pending_;
    std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPCLIENT_H
