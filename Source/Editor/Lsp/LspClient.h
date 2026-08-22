//
// LSP client follow-up. One JSON-RPC 2.0 connection to one running language
// server process (see Transport.h for the raw process/pipe layer this sits
// on top of).
//
// Threading: a background std::jthread runs a blocking read loop
// (Transport::ReadFrame), marshaling each parsed frame onto the main Notcurses event-
// loop thread via ned::ui::EventLoop::Post -- the same
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
// destroyed after ned::ui::EventLoop::Run() has returned -- same
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

#include <nlohmann/json.hpp>

#include "UI/EventLoop.h"

#include "Transport.h"

namespace ned::editor::lsp {

using Json = nlohmann::json;

// background-activity-spinner follow-up. The BackgroundActivity registry
// name every LSP subsystem reports under -- one aggregate spinner, not
// per-language entries. Shared between LspClient's own request tracking and
// LspManager's $/progress handling, which is why it lives here (LspManager.h
// already includes this header, not the other way around).
inline constexpr std::string_view kLspActivityName = "LSP";

// Exactly one of result/error is engaged, matching JSON-RPC 2.0's own
// response shape.
using ResponseCallback    = std::function<void(std::optional<Json> result, std::optional<Json> error)>;
using NotificationHandler = std::function<void(const Json& params)>;

// A server-initiated *request*'s handler -- returns the result to send back
// (workDoneProgress-support follow-up; "window/workDoneProgress/create" is
// the first server->client request any capability this client declares can
// prompt, and its result is simply null).
using RequestHandler = std::function<Json(const Json& params)>;

class LspClient {
  public:
    // Spawns argv as a new language server process. screen must outlive this
    // LspClient (see this file's own header comment on lifetime).
    //
    // handshake-ordering follow-up: this is the constructor real production
    // spawns (LspManager::ClientForLanguage) always use, so it's the one
    // that starts the SendRequest/SendNotification queue-until-initialized
    // gate closed -- see those methods' own doc comments.
    LspClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop);

    // Takes ownership of an already-open Transport directly -- for tests
    // driving a raw pipe pair with no real subprocess involved. The
    // handshake-ordering gate (see SendRequest/SendNotification) starts
    // *open* by default -- a test-injected client (LspManager::
    // SetClientForTesting) never goes through a real initialize/initialized
    // exchange at all, by design, so gating it the same way production
    // clients are would silently queue and drop every existing test's
    // didOpen/etc. notifications instead of writing them. startHandshakeComplete
    // is a test-only seam (public primarily for tests, mirroring
    // SetClientForTesting/DispatchFrame's own precedent) for a test that
    // specifically wants to exercise the gating/queuing behavior itself
    // against a raw pipe pair, with no real subprocess.
    LspClient(Transport transport, ned::ui::EventLoop& eventLoop, bool startHandshakeComplete = true);

    // Member destruction order does the real teardown work -- see header
    // comment. The body only balances the BackgroundActivity registry for
    // requests still pending_ (sent, never answered): their callbacks are
    // documented as dropped uninvoked, so nothing else would ever End the
    // Begin each SendRequest recorded -- a server that died mid-request
    // would otherwise leave the mode-line spinner running forever.
    ~LspClient();

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
    //
    // handshake-ordering follow-up: for a real subprocess-spawned client
    // (the std::vector<std::string> argv constructor), any call here made
    // before "initialized" has actually gone out over the wire is queued
    // and replayed, in order, right after it does -- not written
    // immediately. Per spec the client must not send anything but the
    // initialize request itself before initialized; LspManager::
    // ClientForLanguage fires initialize and returns the client
    // synchronously (the response only arrives on a later event-loop
    // iteration via Post), and every caller downstream (SyncBuffer's
    // didOpen chief among them) calls this on that same client immediately
    // -- a guaranteed race, not a rare one, confirmed live against a real
    // harper-ls: it silently never published a single diagnostic for a
    // buffer whose didOpen it received before initialized, with no error of
    // any kind to explain why. clangd tolerates the same ordering violation
    // (evidently reordering/deferring internally); harper-ls does not.
    // Method-name-recursion (queued calls call this method again once the
    // gate opens) keeps this and SendRequest's own gating logic in exactly
    // one place each rather than a second parallel queue-draining function.
    void SendRequest(const std::string& method, Json params, ResponseCallback callback);

    // handshake-ordering follow-up: same gating as SendRequest, except
    // "initialized" itself is always let through immediately -- it's what
    // *opens* the gate (and flushes anything queued behind it), so gating it
    // too would deadlock every call permanently queued behind it.
    void SendNotification(const std::string& method, Json params);

    // Replaces any existing handler for method. Invoked on the main thread
    // for every server-initiated notification with this method name (e.g.
    // "textDocument/publishDiagnostics").
    void SetNotificationHandler(std::string method, NotificationHandler handler);

    // workDoneProgress-support follow-up: replaces any existing handler for
    // a server-initiated *request* (carries its own "id", expects a
    // response) with this method name; the handler's returned Json is sent
    // back as the response's "result". A server request with no registered
    // handler gets a MethodNotFound (-32601) error response, per JSON-RPC --
    // previously it was silently ignored, defensible only while no declared
    // capability could ever prompt one.
    void SetRequestHandler(std::string method, RequestHandler handler);

    // error-visibility follow-up. Invoked exactly once, on the main thread
    // (via the same eventLoop_.Post the read loop already uses to marshal
    // every frame), the moment the background read loop stops running for
    // any reason -- clean EOF (the server process exited) or a malformed
    // frame (previously a silent `return` in StartReadLoop, either way).
    // reason is a short, human-readable cause. Unset by default, a safe
    // no-op -- same "connect after construction" convention
    // SetNotificationHandler already establishes.
    void SetOnDisconnected(std::function<void(std::string reason)> handler);

    // Public primarily for tests: the real background read loop always
    // reaches this via eventLoop_.Post (see the header comment above), but
    // ned::ui::EventLoop::Post only ever enqueues onto its own posted_ queue --
    // confirmed by reading app.cpp, not assumed -- there's no synchronous
    // fallback the way the *static* PostEventOrExecute helper has, so a
    // test would need a real, running EventLoop::Run() loop to ever see
    // a Post-driven call actually happen. Calling this directly instead
    // exercises the exact same correlation/dispatch logic without needing
    // that.
    void DispatchFrame(const std::string& frameText);

  private:
    void StartReadLoop();

    std::jthread readThread_; // declared before transport_ -- see header comment
    Transport    transport_;

    ned::ui::EventLoop& eventLoop_;

    int                                                  nextRequestId_ = 1;
    std::unordered_map<int, ResponseCallback>            pending_;
    std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
    std::unordered_map<std::string, RequestHandler>      requestHandlers_;
    std::function<void(std::string reason)>              onDisconnected_; // see SetOnDisconnected

    // handshake-ordering follow-up: see SendRequest/SendNotification and the
    // two constructors' own doc comments. handshakeComplete_ defaults to
    // false only for the real-subprocess constructor; pendingUntilHandshake_
    // holds every SendRequest/SendNotification call made before the gate
    // opens, replayed in order once it does.
    bool                                  handshakeComplete_ = true;
    std::vector<std::function<void()>>    pendingUntilHandshake_;
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPCLIENT_H
