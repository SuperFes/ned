//
// DAP client — slice 1. One connection to one running debug-adapter process.
//
// Deliberately reuses Lsp/Transport.h unmodified: DAP inherited LSP's exact
// base framing ("Content-Length: N\r\n\r\n" + JSON payload), so the framing
// layer is identical — exactly the reuse the task runner already proved out
// for ChildProcess, one layer up. What differs is everything above the
// framing: DAP is NOT JSON-RPC. Its envelope is {"seq": N, "type":
// "request" | "response" | "event", ...}; a request carries "command"/
// "arguments", a response carries "request_seq"/"success"/"command" plus
// "body" (success) or "message" (failure), and unsolicited "event" messages
// ("initialized", "stopped", "terminated", ...) are the heart of the
// protocol, not an edge case — which is why this is its own class rather
// than LspClient with different method names.
//
// Threading, lifetime, and member-declaration order all mirror LspClient
// exactly (background jthread read loop marshaling onto the main thread via
// ned::ui::EventLoop::Post; transport_ declared after readThread_ so its
// destructor closes the fds that unblock the read thread's blocking
// ReadFrame) — see LspClient.h's own header comment for the full reasoning
// behind each; none of it is repeated here because none of it differs.
//
// lsp-use-after-free follow-up: that includes alive_ (see LspClient.h's own
// header comment, corrected 2026-08-26) -- the earlier claim that this class
// is "only ever destroyed after EventLoop::Run() has returned" was false for
// LspClient's mid-session respawn path, confirmed live via ASan, and nothing
// about DapManager's own single-session model makes DapClient immune to the
// same hazard (a Post()ed callback from readThread_/stderrThread_ that
// outlives the object, freed by DapManager::EndSession, whether immediately
// or after some delay -- no delay is actually safe, only alive_ is).
//
// lsp-stderr-capture follow-up (extended to DAP): stderrThread_ mirrors
// LspClient's own stderrThread_ exactly -- a second blocking read loop over
// lsp::Transport::StderrFd(), declared alongside readThread_ before
// transport_ for the same destruction-order reason. The real-subprocess
// constructor passes captureStderr=true; the Transport-taking test
// constructor never captures (StartStderrReadLoop is a no-op when
// StderrFd() < 0).
//
// async-write-queue follow-up (extended to DAP, for consistency -- no live
// freeze reported against this client specifically): mirrors LspClient's own
// writeThread_/EnqueueWrite/PrepareForGracefulShutdown exactly -- see
// LspClient.h's own header comment for the full reasoning. DapManager::
// StopSession sends a best-effort "disconnect" request immediately before
// EndSession destroys the client (mirroring LspManager::Shutdown's own
// "shutdown"+"exit" courtesy pair) -- confirmed live by a real test failure
// during this transplant: without PrepareForGracefulShutdown, that
// SendRequest-then-immediately-destroy sequence raced the destructor's
// implicit request_stop() against writeThread_ actually writing the queued
// disconnect frame, silently dropping it more often than not.
//

#ifndef NED_EDITOR_DAP_DAPCLIENT_H
#define NED_EDITOR_DAP_DAPCLIENT_H

#include <atomic>
#include <chrono>
#include <condition_variable> // condition_variable_any -- see LspClient.h's own comment on writeCv_
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

#include "Editor/Lsp/Transport.h"

namespace ned::editor::dap {

using Json = nlohmann::json;

// success=true: body is the response's "body" (an empty object if the
// adapter sent none). success=false: message is the response's own
// human-readable "message" (or a generic fallback) — DAP reports failure via
// success/message on the response envelope itself, not a JSON-RPC error
// object.
using ResponseCallback = std::function<void(bool success, Json body, std::string message)>;
using EventHandler     = std::function<void(const Json& body)>;

class DapClient {
  public:
    // Spawns argv as a new debug-adapter process. eventLoop must outlive
    // this DapClient (see header comment).
    DapClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop);

    // Takes ownership of an already-open Transport directly — for tests
    // driving a raw pipe pair with no real subprocess involved, mirroring
    // LspClient's own test constructor.
    DapClient(lsp::Transport transport, ned::ui::EventLoop& eventLoop);

    // lsp-use-after-free follow-up: no longer = default -- the body flips
    // alive_ to false as its first statement (see LspClient.h's own header
    // comment); member destruction order still does the rest of the real
    // teardown work, same as before -- see LspClient.h.
    ~DapClient();

    DapClient(const DapClient&)            = delete;
    DapClient& operator=(const DapClient&) = delete;
    DapClient(DapClient&&)                 = delete;
    DapClient& operator=(DapClient&&)      = delete;

    // Sends {"seq": <fresh>, "type": "request", "command": command,
    // "arguments": arguments}. callback runs on the main thread once the
    // matching response (by "request_seq") arrives; dropped uninvoked if
    // this DapClient is destroyed first, matching LspClient::SendRequest's
    // own "abandoned at shutdown" convention.
    void SendRequest(const std::string& command, Json arguments, ResponseCallback callback);

    // Replaces any existing handler for event (e.g. "stopped",
    // "terminated"). Invoked on the main thread with the event's "body" (an
    // empty object if the adapter sent none).
    void SetEventHandler(std::string event, EventHandler handler);

    // Same contract as LspClient::SetOnDisconnected — invoked exactly once,
    // on the main thread, when the read loop stops for any reason.
    void SetOnDisconnected(std::function<void(std::string reason)> handler);

    // Public primarily for tests, for exactly the reasons
    // LspClient::DispatchFrame documents (EventLoop::Post only enqueues; a
    // test with no running Run() loop calls this directly instead).
    void DispatchFrame(const std::string& frameText);

    // subprocess-hang-protection follow-up -- see LspClient::ExpireStaleRequests's
    // identical doc comment; DAP has no BackgroundActivity spinner to pair, so
    // this is otherwise the same shape (synthetic failure via the existing
    // success=false/message callback branch, no new handling needed at any
    // call site). Real callers take ProcessTimeouts.h's
    // ProtocolRequestTimeoutMs() as their default -- the same
    // Janet-configurable setting LSP/ACP requests share (ChildProcess-
    // hang-protection-round-2 follow-up).
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

    // async-write-queue follow-up: see LspClient::PrepareForGracefulShutdown's
    // identical doc comment -- call this immediately before a best-effort
    // courtesy request (e.g. DapManager::StopSession's "disconnect") that
    // must actually reach the wire before this DapClient is destroyed.
    void PrepareForGracefulShutdown();

  private:
    void StartReadLoop();
    void StartStderrReadLoop(); // lsp-stderr-capture follow-up -- see header comment
    void StartWriteLoop();      // async-write-queue follow-up -- see header comment

    // async-write-queue follow-up: enqueues frame for writeThread_ to send,
    // returning immediately -- replaces the direct transport_.WriteFrame
    // call. The one call site (SendRequest) runs on the main thread only, so
    // enqueue order is call order is on-wire order.
    void EnqueueWrite(std::string frame);

    // lsp-use-after-free follow-up: see LspClient.h's own header comment on
    // alive_ and this file's header comment above.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::jthread   readThread_;   // declared before transport_ — see LspClient.h
    std::jthread   stderrThread_; // ditto -- lsp-stderr-capture follow-up
    lsp::Transport transport_;

    ned::ui::EventLoop& eventLoop_;

    // async-write-queue follow-up: writeThread_ is declared *after*
    // transport_ (opposite of readThread_/stderrThread_ above) so it
    // destructs *before* transport_ -- see LspClient.h's own header comment.
    // writeMutex_/writeCv_/writeQueue_ must outlive writeThread_, so they're
    // declared ahead of it here.
    std::mutex                  writeMutex_;
    std::condition_variable_any writeCv_;
    std::deque<std::string>     writeQueue_;
    std::atomic<bool>       drainQueueOnStop_ = false; // see PrepareForGracefulShutdown
    std::jthread            writeThread_;

    struct PendingRequest {
        ResponseCallback                      callback;
        std::chrono::steady_clock::time_point sentAt;
    };

    int                                           nextSeq_ = 1;
    std::unordered_map<int, PendingRequest>       pending_; // keyed by the request's own seq
    std::unordered_map<std::string, EventHandler> eventHandlers_;
    std::function<void(std::string reason)>       onDisconnected_;
};

} // namespace ned::editor::dap

#endif // NED_EDITOR_DAP_DAPCLIENT_H
