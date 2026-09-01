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
// Lifetime (lsp-use-after-free follow-up, corrected 2026-08-26): the
// paragraph this replaced claimed LspClient is only ever destroyed after
// EventLoop::Run() has returned. That is false for LspManager's own
// mid-session respawn path -- confirmed live via ASan (heap-use-after-free,
// LspClient.cpp's own StartReadLoop lambda, one background thread's already-
// Post()ed callback still pending when a *different* Post()ed callback --
// LspManager::ExpireStaleRequests's periodic retired_.clear() -- freed this
// same object first). Two independent background threads (or a background
// thread and a periodic timer) Post() against EventLoop with no ordering
// guarantee between them, so "wait one more tick before freeing" is not
// actually safe. alive_ is what makes this safe regardless of timing: a
// std::shared_ptr<bool>, flipped to false as literally the first statement
// in ~LspClient(), captured *by value* (a second owning reference, so its
// storage itself never dangles) alongside `this` in every eventLoop_.Post
// lambda below. Each posted lambda checks `*alive` before touching `this`
// at all -- false means the object is gone or going, and the lambda safely
// no-ops instead of reading freed memory. This is the standard fix for
// "background thread posts a callback that outlives the object it
// captured," not a timing workaround.
//
// Member declaration order below is load-bearing, not just style: destroying
// transport_ (which happens before readThread_/stderrThread_, since C++
// destroys members in reverse declaration order, and transport_ is declared
// *after* both) is what makes their destructor-driven request_stop()+join()
// actually terminate promptly -- Transport's own destructor closes this
// end's fds and kills+reaps the child process, which is what makes each
// thread's in-flight, otherwise-unstoppable blocking read finally return
// (EOF, once the child's stdout/stderr pipes have no writers left). A
// stop_token alone cannot interrupt a blocking read() -- this ordering is
// the actual interruption mechanism, not a defensive extra.
//
// lsp-stderr-capture follow-up: stderrThread_ mirrors readThread_'s own
// shape exactly (a blocking read loop, Post-marshaled onto the main thread)
// but reads Transport::StderrFd() instead of frame-parsing Transport::
// ReadFrame() -- raw, unframed server-process stderr output, not JSON-RPC.
// Only started when StderrFd() >= 0, i.e. only for the real-subprocess
// constructor (the Transport-taking constructor never captures stderr,
// matching its own default -- test-injected clients have nothing to drain).
//
// async-write-queue follow-up: every SendRequest/SendNotification/server-
// request-response send used to call transport_.WriteFrame directly on the
// main thread -- synchronous, and unboundedly blocking (up to
// ProtocolStallTimeoutMs()) whenever a server's stdin pipe backs up. Two
// real, gdb-confirmed live freezes traced to exactly this (a rapid-typing
// didChange flood, and a periodic background-sync didOpen stall against a
// slow server) -- see LspManager's own sync-debounce/background-sync
// comments. Writes now go through EnqueueWrite -> writeQueue_, drained by a
// dedicated writeThread_, so a stalled write only ever blocks that thread.
// writeThread_ needs the *opposite* member-order relationship transport_ has
// with readThread_/stderrThread_ above: it must finish (and join) *before*
// transport_ destructs, not after, or a graceful-shutdown drain (see
// PrepareForGracefulShutdown) would attempt to write through an
// already-closed transport. So writeThread_ (and writeMutex_/writeCv_/
// writeQueue_/drainQueueOnStop_, which must outlive it) are declared *after*
// transport_ -- the mirror image of the readThread_/stderrThread_ rule above.
// Do not "fix" one ordering to match the other; they're deliberately
// opposite for opposite reasons.
//

#ifndef NED_EDITOR_LSP_LSPCLIENT_H
#define NED_EDITOR_LSP_LSPCLIENT_H

#include <atomic>
#include <chrono>
#include <condition_variable> // condition_variable_any, for its stop_token-aware wait() overload -- see writeCv_'s own comment
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/ProcessTimeouts.h"
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

// subprocess-hang-protection follow-up. A server that simply never answers a
// request (as opposed to a stalled/malformed connection, which
// Transport::ReadFrame's own stall timeout already catches) previously
// left that request pending_ forever -- no error, no timeout, a
// permanently-spinning hover/completion/code-action with nothing to show for
// it. Generous on purpose: a slow rename/format-on-save on a huge file
// should never hit this; it exists only to eventually resolve a request that
// will truly never answer. ExpireStaleRequests takes this as a parameter
// (not baked in) so LspManager's real sweep and this file's own tests can
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

    // subprocess-hang-protection follow-up. Erases every pending_ entry
    // older than maxAge, invoking its callback with a synthetic JSON-RPC
    // timeout error (matching the real error shape a server's own response
    // would use, so no existing caller needs new handling) and pairing
    // EndBackgroundActivity the same way DispatchFrame's own response path
    // already does. Public (not driven internally by a timer -- this class
    // has no timer of its own) so LspManager's sweep, wired into
    // WindowManager's existing background tick, can call it, and so tests
    // can pass a much shorter maxAge than the real default.
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

    // async-write-queue follow-up: marks this client for graceful shutdown --
    // guarantees any currently-queued or subsequently-enqueued frame (in
    // practice, LspManager::Shutdown()'s courtesy "shutdown" request + "exit"
    // notification) is actually attempted by writeThread_ before it stops,
    // instead of the destructor's ordinary best-effort/no-drain policy (see
    // header comment). Call this immediately before those two calls. Not
    // meant for any other caller -- ordinary mid-session teardown
    // (LspManager::ClientDisconnected) must NOT call this, since draining a
    // queue against a connection that's already dying/dead is exactly the
    // main-thread stall this whole mechanism exists to avoid, and there's
    // nothing worth delivering to a dead connection anyway.
    void PrepareForGracefulShutdown();

  private:
    void StartReadLoop();
    void StartStderrReadLoop(); // lsp-stderr-capture follow-up -- see header comment
    void StartWriteLoop();      // async-write-queue follow-up -- see header comment

    // async-write-queue follow-up: enqueues frame for writeThread_ to send,
    // returning immediately -- replaces every direct transport_.WriteFrame
    // call. All three call sites (server-request responses, SendRequest,
    // SendNotification) run on the main thread only, so enqueue order is
    // call order is on-wire order -- unchanged from the old synchronous
    // behavior, just no longer blocking to get there.
    void EnqueueWrite(std::string frame);

    // lsp-use-after-free follow-up: see this file's own header comment.
    // Declaration position doesn't matter for correctness (a shared_ptr's
    // pointee lifetime is independent of where the shared_ptr variable
    // itself lives), but it's declared first for visibility -- this is the
    // actual safety mechanism every Post() lambda below relies on.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::jthread readThread_;       // declared before transport_ -- see header comment
    std::jthread stderrThread_;     // ditto -- lsp-stderr-capture follow-up
    Transport    transport_;

    ned::ui::EventLoop& eventLoop_;

    // async-write-queue follow-up: writeThread_ is declared *after*
    // transport_ (opposite of readThread_/stderrThread_ above) so it
    // destructs *before* transport_ -- see header comment. writeMutex_/
    // writeCv_/writeQueue_/drainQueueOnStop_ must outlive writeThread_, so
    // they're declared ahead of it here.
    std::mutex writeMutex_;
    // condition_variable_any, not condition_variable: plain condition_variable::wait
    // never wakes on request_stop() alone -- only notify_one()/notify_all() wakes
    // it, and request_stop() calls neither. condition_variable_any's stop_token-aware
    // wait(lock, stopToken, predicate) overload registers its own internal
    // stop_callback that does the notifying -- without this, ~LspClient()'s implicit
    // join() on writeThread_ hangs forever whenever the writer is idly waiting on an
    // empty queue at destruction time (confirmed live: nearly every existing
    // LspClientTest.cpp test hung on this before the fix).
    std::condition_variable_any writeCv_;
    std::deque<std::string>     writeQueue_;
    std::atomic<bool>       drainQueueOnStop_ = false; // see PrepareForGracefulShutdown
    std::jthread            writeThread_;

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

#endif // NED_EDITOR_LSP_LSPCLIENT_H
