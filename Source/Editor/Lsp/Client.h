//
// LSP client follow-up. One JSON-RPC 2.0 connection to one running language
// server process (see Transport.h for the raw process/pipe layer this sits
// on top of).
//
// one-connection-class follow-up: the threading/lifetime machinery this
// class used to hand-roll (background read loop, stderr loop, async write
// queue, the alive_ use-after-free guard, and the load-bearing member-
// declaration order all three of Lsp/Dap/Acp Client once carried) now lives
// once, in Editor/Protocol/FramedConnection.h, which connection_ below owns
// as a single member — see that file's own header comment for the full
// reasoning, including why a use-after-free hazard fixed there (lsp-use-
// after-free follow-up, corrected 2026-08-26 -- a real, ASan-confirmed
// heap-use-after-free on Manager's mid-session respawn path) needs no
// separate guard here: connection_'s own alive_ is checked before its
// onFrame_/onDisconnected_ callbacks are ever invoked, and since connection_
// is a member of this Client (constructed and destroyed alongside it), a
// callback that finds it false can only run after this Client itself has
// already finished being destroyed -- by which point it never touches this
// Client's `this` at all, having already returned.
//
// This class keeps only what's genuinely protocol-specific: JSON-RPC
// envelope construction/correlation, the initialize/initialized handshake
// gate, and the BackgroundActivity spinner bookkeeping around a pending
// request's lifetime.
//

#ifndef NED_EDITOR_LSP_CLIENT_H
#define NED_EDITOR_LSP_CLIENT_H

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/ProcessTimeouts.h"
#include "Editor/Protocol/FramedConnection.h"
#include "UI/EventLoop.h"

#include "Transport.h"

namespace ned::editor::lsp {

using Json = nlohmann::json;

// background-activity-spinner follow-up. The BackgroundActivity registry
// name every LSP subsystem reports under -- one aggregate spinner, not
// per-language entries. Shared between Client's own request tracking and
// Manager's $/progress handling, which is why it lives here (Manager.h
// already includes this header, not the other way around).
inline constexpr std::string_view kLspActivityName = "LSP";

// subprocess-hang-protection follow-up. A server that simply never answers a
// request (as opposed to a stalled/malformed connection, which
// Transport::ReadFrame's own stall timeout already catches) previously
// left that request pending_ forever -- no error, no timeout, a
// permanently-spinning hover/completion/code-action with nothing to show for
// it. Generous on purpose: a slow rename/format-on-save on a huge file
// should never hit this; it exists only to eventually resolve a request that
// will truly never answer. ExpireStaleRequests takes this as a parameter
// (not baked in) so Manager's real sweep and this file's own tests can
// use different values; real callers take ProcessTimeouts.h's
// ProtocolRequestTimeoutMs() as their default below
// (ChildProcess-hang-protection-round-2 follow-up: Janet-configurable,
// replacing this file's old kDefaultRequestTimeout compile-time constant).

// Exactly one of result/error is engaged, matching JSON-RPC 2.0's own
// response shape.
using ResponseCallback    = std::function<void(std::optional<Json> result, std::optional<Json> error)>;
using NotificationHandler = std::function<void(const Json& params)>;

// A server-initiated *request*'s handler -- returns the result to send back
// (workDoneProgress-support follow-up; "window/workDoneProgress/create" is
// the first server->client request any capability this client declares can
// prompt, and its result is simply null).
using RequestHandler = std::function<Json(const Json& params)>;

class Client {
  public:
    // Spawns argv as a new language server process. eventLoop must outlive
    // this Client (see FramedConnection.h's own lifetime contract).
    //
    // handshake-ordering follow-up: this is the constructor real production
    // spawns (Manager::ClientForLanguage) always use, so it's the one
    // that starts the SendRequest/SendNotification queue-until-initialized
    // gate closed -- see those methods' own doc comments.
    Client(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop);

    // Takes ownership of an already-open Transport directly -- for tests
    // driving a raw pipe pair with no real subprocess involved. The
    // handshake-ordering gate (see SendRequest/SendNotification) starts
    // *open* by default -- a test-injected client (Manager::
    // SetClientForTesting) never goes through a real initialize/initialized
    // exchange at all, by design, so gating it the same way production
    // clients are would silently queue and drop every existing test's
    // didOpen/etc. notifications instead of writing them. startHandshakeComplete
    // is a test-only seam (public primarily for tests, mirroring
    // SetClientForTesting/DispatchFrame's own precedent) for a test that
    // specifically wants to exercise the gating/queuing behavior itself
    // against a raw pipe pair, with no real subprocess.
    Client(Transport transport, ned::ui::EventLoop& eventLoop, bool startHandshakeComplete = true);

    // The only teardown work left that isn't connection_'s own destructor's
    // job: a request still pending_ (sent, never answered) has its
    // BackgroundActivity Begin balanced here, since its callback is
    // documented as dropped uninvoked (see SendRequest) -- without this, a
    // server that died mid-request would leave the mode-line spinner
    // running forever. connection_ (declared last, so it destructs first --
    // see this file's own header comment) is already torn down by the time
    // this body runs, which is fine: nothing here touches it.
    ~Client();

    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&)                 = delete;
    Client& operator=(Client&&)      = delete;

    // Sends a JSON-RPC request with a freshly allocated id. callback runs on
    // the main thread once the matching response arrives; if this Client
    // is destroyed first, callback is simply dropped, uninvoked (matches
    // this class's own "abandoned at shutdown" convention -- see header
    // comment).
    //
    // handshake-ordering follow-up: for a real subprocess-spawned client
    // (the std::vector<std::string> argv constructor), any call here made
    // before "initialized" has actually gone out over the wire is queued
    // and replayed, in order, right after it does -- not written
    // immediately. Per spec the client must not send anything but the
    // initialize request itself before initialized; Manager::
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

    // error-visibility follow-up. Invoked exactly once, on the main thread,
    // the moment the connection stops running for any reason -- clean EOF
    // (the server process exited) or a malformed frame. reason is a short,
    // human-readable cause. Unset by default, a safe no-op -- same "connect
    // after construction" convention SetNotificationHandler already
    // establishes.
    void SetOnDisconnected(std::function<void(std::string reason)> handler);

    // Public primarily for tests: FramedConnection's own SetOnFrame callback
    // reaches this via EventLoop::Post; calling it directly exercises the
    // same correlation/dispatch logic without needing a running Run() loop.
    void DispatchFrame(const std::string& frameText);

    // subprocess-hang-protection follow-up. Erases every pending_ entry
    // older than maxAge, invoking its callback with a synthetic JSON-RPC
    // timeout error (matching the real error shape a server's own response
    // would use, so no existing caller needs new handling) and pairing
    // EndBackgroundActivity the same way DispatchFrame's own response path
    // already does. Public (not driven internally by a timer -- this class
    // has no timer of its own) so Manager's sweep, wired into
    // WindowManager's existing background tick, can call it, and so tests
    // can pass a much shorter maxAge than the real default.
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

    // async-write-queue follow-up: marks this client for graceful shutdown --
    // guarantees any currently-queued or subsequently-enqueued frame (in
    // practice, Manager::Shutdown()'s courtesy "shutdown" request + "exit"
    // notification) is actually attempted before it stops, instead of the
    // destructor's ordinary best-effort/no-drain policy (see
    // FramedConnection::PrepareForGracefulShutdown). Call this immediately
    // before those two calls. Not meant for any other caller -- ordinary
    // mid-session teardown (Manager::ClientDisconnected) must NOT call this,
    // since draining a queue against a connection that's already dying/dead
    // is exactly the main-thread stall this whole mechanism exists to
    // avoid, and there's nothing worth delivering to a dead connection
    // anyway.
    void PrepareForGracefulShutdown();

  private:
    // Declared first (destructs last, per this file's own header comment on
    // the reverse-declaration-order rule) purely for readability -- nothing
    // else here depends on connection_'s own destruction timing relative to
    // pending_/notificationHandlers_/etc, since a Post()ed callback can
    // never interleave with this class's own (synchronous, single-threaded)
    // destructor body; see this file's own header comment.
    protocol::FramedConnection<Transport> connection_;

    struct PendingRequest {
        ResponseCallback                      callback;
        std::chrono::steady_clock::time_point sentAt;
    };

    int                                                  nextRequestId_ = 1;
    std::unordered_map<int, PendingRequest>              pending_;
    std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
    std::unordered_map<std::string, RequestHandler>      requestHandlers_;
    std::function<void(std::string reason)>              onDisconnected_; // see SetOnDisconnected

    // handshake-ordering follow-up: see SendRequest/SendNotification and the
    // two constructors' own doc comments. handshakeComplete_ defaults to
    // false only for the real-subprocess constructor; pendingUntilHandshake_
    // holds every SendRequest/SendNotification call made before the gate
    // opens, replayed in order once it does.
    bool                               handshakeComplete_ = true;
    std::vector<std::function<void()>> pendingUntilHandshake_;
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_CLIENT_H
