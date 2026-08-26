//
// ACP client, slice 1. One JSON-RPC 2.0 connection to one running ACP agent
// process (see Transport.h for the raw process/newline-framing layer this
// sits on top of).
//
// Threading, lifetime, and member-declaration order all mirror
// Lsp/LspClient.h exactly (background jthread read loop marshaling onto the
// main thread via ned::ui::EventLoop::Post; destroy only after
// EventLoop::Run() has returned; transport_ declared after readThread_ so
// its destructor's fd close is what unblocks the background thread's
// in-flight blocking Transport::ReadMessage()) -- see that file's own header
// comment for the full reasoning, none of which differs here and none of
// which is repeated.
//
// Two real differences from LspClient:
//
//   - No handshake-ordering gate. LSP has a hard rule (nothing but
//     "initialize" itself may be sent before "initialized" goes out) that
//     bit a real server (harper-ls) in practice, which is why LspClient
//     queues calls until that gate opens. ACP's "initialize" has no such
//     restriction on this client's own subsequent calls, so there is nothing
//     to gate.
//
//   - RequestHandler is async-capable. LSP's server-initiated requests
//     (window/workDoneProgress/create, the only one this codebase answers)
//     always resolve synchronously, so LspClient::RequestHandler is a plain
//     Json(const Json&). ACP's agent-initiated requests include
//     session/request_permission, which genuinely needs to wait on a live
//     user keystroke (a whole InteractiveRequest round-trip through
//     BufferView) before it has an answer -- so this RequestHandler instead
//     takes a `respond` continuation, callable either synchronously inline
//     (fs/read_text_file) or much later (a permission prompt). See
//     AcpManager.h for the lifetime contract that follows from "much
//     later": a `respond` continuation must never be invoked after the
//     AcpClient that handed it out has been destroyed.
//
// lsp-stderr-capture follow-up (extended to ACP): stderrThread_ mirrors
// LspClient's own stderrThread_ exactly -- a second blocking read loop over
// Transport::StderrFd(), declared alongside readThread_ before transport_
// for the same destruction-order reason. The real-subprocess constructor
// passes captureStderr=true; the Transport-taking test constructor never
// captures (StartStderrReadLoop is a no-op when StderrFd() < 0).
//

#ifndef NED_EDITOR_ACP_ACPCLIENT_H
#define NED_EDITOR_ACP_ACPCLIENT_H

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/ProcessTimeouts.h"
#include "UI/EventLoop.h"

#include "Transport.h"

namespace ned::editor::acp {

using Json = nlohmann::json;

// Exactly one of result/error is engaged, matching JSON-RPC 2.0's own
// response shape.
using ResponseCallback = std::function<void(std::optional<Json> result, std::optional<Json> error)>;
// Agent -> client notification (e.g. "session/update"). Invoked on the main
// thread.
using NotificationHandler = std::function<void(const Json& params)>;
// Agent -> client request. respond must be called exactly once, with either
// a result or an error engaged (never both) -- see this file's own header
// comment on why this differs from a plain synchronous return.
using RespondFn      = std::function<void(std::optional<Json> result, std::optional<Json> error)>;
using RequestHandler = std::function<void(const Json& params, RespondFn respond)>;

class AcpClient {
  public:
    // Spawns argv as a new agent process. eventLoop must outlive this
    // AcpClient (see header comment).
    AcpClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop);

    // Takes ownership of an already-open Transport directly -- for tests
    // driving a raw pipe pair with no real subprocess involved, mirroring
    // LspClient's own test constructor.
    AcpClient(Transport transport, ned::ui::EventLoop& eventLoop);

    ~AcpClient() = default; // member destruction order does the real work -- see header comment

    AcpClient(const AcpClient&)            = delete;
    AcpClient& operator=(const AcpClient&) = delete;
    // Not movable: the background thread's lambda captures `this` directly.
    AcpClient(AcpClient&&)            = delete;
    AcpClient& operator=(AcpClient&&) = delete;

    // Sends a JSON-RPC request with a freshly allocated id. callback runs on
    // the main thread once the matching response arrives; if this AcpClient
    // is destroyed first, callback is simply dropped, uninvoked -- matches
    // LspClient::SendRequest's own "abandoned at shutdown" convention.
    void SendRequest(const std::string& method, Json params, ResponseCallback callback);

    // Sends a JSON-RPC notification (no "id", no response expected) -- e.g.
    // "session/cancel".
    void SendNotification(const std::string& method, Json params);

    // Replaces any existing handler for method. Invoked on the main thread
    // for every agent-initiated notification with this method name (e.g.
    // "session/update").
    void SetNotificationHandler(std::string method, NotificationHandler handler);

    // Replaces any existing handler for method. Invoked on the main thread
    // for every agent-initiated request with this method name (e.g.
    // "fs/read_text_file", "session/request_permission"). A request with no
    // registered handler gets an immediate MethodNotFound (-32601) error
    // response -- this is what makes leaving a capability's methods
    // unhandled a safe, spec-legal no-op rather than a hang, *provided* that
    // capability was also left undeclared in this client's own "initialize"
    // params (an agent is expected not to invoke a method it wasn't told the
    // client supports; AcpManager is what actually makes that declaration).
    void SetRequestHandler(std::string method, RequestHandler handler);

    // Invoked exactly once, on the main thread, the moment the background
    // read loop stops running for any reason -- clean EOF (the agent process
    // exited) or a malformed message. reason is a short, human-readable
    // cause. Unset by default, a safe no-op -- mirrors
    // LspClient::SetOnDisconnected exactly.
    void SetOnDisconnected(std::function<void(std::string reason)> handler);

    // Public primarily for tests -- mirrors LspClient::DispatchFrame's own
    // "public primarily for tests" precedent (see that method's doc comment)
    // for exactly the same reason: EventLoop::Post only enqueues, so a test
    // with no running EventLoop::Run() loop calls this directly instead.
    void DispatchFrame(const std::string& frameText);

    // subprocess-hang-protection follow-up -- see LspClient::ExpireStaleRequests's
    // identical doc comment. Real callers take ProcessTimeouts.h's
    // ProtocolRequestTimeoutMs() as their default (ChildProcess-hang-
    // protection-round-2 follow-up).
    //
    // ACP round-1-live-validation follow-up: unlike LSP/DAP's one-shot
    // request/response calls, a "session/prompt" request can legitimately sit
    // pending for an entire agent turn -- long tool executions and streaming
    // session/update chunks are normal, not a hang. A pending request is
    // therefore only expired if *no* frame at all (of any kind) has arrived
    // from the agent for maxAge, not merely if the one specific request has
    // been outstanding that long -- see lastActivityAt_/DispatchFrame.
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

  private:
    void StartReadLoop();
    void StartStderrReadLoop(); // lsp-stderr-capture follow-up -- see header comment

    std::jthread readThread_;   // declared before transport_ -- see header comment
    std::jthread stderrThread_; // ditto -- lsp-stderr-capture follow-up
    Transport    transport_;

    ned::ui::EventLoop& eventLoop_;

    struct PendingRequest {
        ResponseCallback                      callback;
        std::chrono::steady_clock::time_point sentAt;
    };

    int                                                  nextRequestId_ = 1;
    std::unordered_map<int, PendingRequest>              pending_;
    std::chrono::steady_clock::time_point                lastActivityAt_ = std::chrono::steady_clock::now(); // see ExpireStaleRequests's doc comment
    std::unordered_map<std::string, NotificationHandler> notificationHandlers_;
    std::unordered_map<std::string, RequestHandler>      requestHandlers_;
    std::function<void(std::string reason)>              onDisconnected_; // see SetOnDisconnected
};

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_ACPCLIENT_H
