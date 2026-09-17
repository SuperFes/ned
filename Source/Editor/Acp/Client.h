//
// ACP client, slice 1. One JSON-RPC 2.0 connection to one running ACP agent
// process (see Transport.h for the raw process/newline-framing layer this
// sits on top of).
//
// one-connection-class follow-up: the threading/lifetime machinery this
// class used to hand-roll (background read loop, stderr loop, async write
// queue, the alive_ use-after-free guard, and the load-bearing member-
// declaration order all three of Lsp/Dap/Acp Client once carried) now lives
// once, in Editor/Protocol/FramedConnection.h, which connection_ below owns
// as a single member — see that file's own header comment for the full
// reasoning. This class keeps only what's genuinely protocol-specific:
// JSON-RPC envelope construction/correlation, and two real differences from
// Lsp::Client that predate this refactor and are unaffected by it:
//
//   - No handshake-ordering gate. LSP has a hard rule (nothing but
//     "initialize" itself may be sent before "initialized" goes out) that
//     bit a real server (harper-ls) in practice, which is why Lsp::Client
//     queues calls until that gate opens. ACP's "initialize" has no such
//     restriction on this client's own subsequent calls, so there is nothing
//     to gate.
//
//   - RequestHandler is async-capable. LSP's server-initiated requests
//     (window/workDoneProgress/create, the only one this codebase answers)
//     always resolve synchronously, so Lsp::Client::RequestHandler is a
//     plain Json(const Json&). ACP's agent-initiated requests include
//     session/request_permission, which genuinely needs to wait on a live
//     user keystroke (a whole InteractiveRequest round-trip through
//     BufferView) before it has an answer -- so this RequestHandler instead
//     takes a `respond` continuation, callable either synchronously inline
//     (fs/read_text_file) or much later (a permission prompt). See
//     Manager.h for the lifetime contract that follows from "much
//     later": a `respond` continuation must never be invoked after the
//     Client that handed it out has been destroyed.
//

#ifndef NED_EDITOR_ACP_CLIENT_H
#define NED_EDITOR_ACP_CLIENT_H

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

class Client {
  public:
    // Spawns argv as a new agent process. eventLoop must outlive this
    // Client (see FramedConnection.h's own lifetime contract).
    Client(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop);

    // Takes ownership of an already-open Transport directly -- for tests
    // driving a raw pipe pair with no real subprocess involved.
    Client(Transport transport, ned::ui::EventLoop& eventLoop);

    ~Client() = default; // connection_'s own destructor does the real teardown work — see FramedConnection.h

    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&)                 = delete;
    Client& operator=(Client&&)      = delete;

    // Sends a JSON-RPC request with a freshly allocated id. callback runs on
    // the main thread once the matching response arrives; if this Client
    // is destroyed first, callback is simply dropped, uninvoked.
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
    // client supports; Manager is what actually makes that declaration).
    void SetRequestHandler(std::string method, RequestHandler handler);

    // Invoked exactly once, on the main thread, when the connection stops
    // running for any reason.
    void SetOnDisconnected(std::function<void(std::string reason)> handler);

    // Public primarily for tests -- FramedConnection's own SetOnFrame
    // callback reaches this via EventLoop::Post; calling it directly
    // exercises the same correlation/dispatch logic without needing a
    // running Run() loop.
    void DispatchFrame(const std::string& frameText);

    // subprocess-hang-protection follow-up -- see
    // Lsp::Client::ExpireStaleRequests's identical doc comment.
    //
    // ACP round-1-live-validation follow-up: unlike LSP/DAP's one-shot
    // request/response calls, a "session/prompt" request can legitimately sit
    // pending for an entire agent turn -- long tool executions and streaming
    // session/update chunks are normal, not a hang. A pending request is
    // therefore only expired if *no* frame at all (of any kind) has arrived
    // from the agent for maxAge, not merely if the one specific request has
    // been outstanding that long -- see lastActivityAt_/DispatchFrame.
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

    // async-write-queue follow-up: see FramedConnection::PrepareForGracefulShutdown's
    // doc comment -- call this immediately before a best-effort courtesy
    // request (e.g. Manager::StopSession's "session/close") that must
    // actually reach the wire before this Client is destroyed.
    void PrepareForGracefulShutdown();

  private:
    protocol::FramedConnection<Transport> connection_;

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

#endif // NED_EDITOR_ACP_CLIENT_H
