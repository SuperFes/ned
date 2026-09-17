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
// than Lsp::Client with different method names.
//
// one-connection-class follow-up: the threading/lifetime machinery this
// class used to hand-roll (background read loop, stderr loop, async write
// queue, the alive_ use-after-free guard, and the load-bearing member-
// declaration order all three of Lsp/Dap/Acp Client once carried) now lives
// once, in Editor/Protocol/FramedConnection.h, which connection_ below owns
// as a single member — see that file's own header comment for the full
// reasoning. This class keeps only what's genuinely protocol-specific:
// seq/type envelope construction, request/response correlation by
// request_seq, and event dispatch.
//

#ifndef NED_EDITOR_DAP_CLIENT_H
#define NED_EDITOR_DAP_CLIENT_H

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/ProcessTimeouts.h"
#include "Editor/Protocol/FramedConnection.h"
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

class Client {
  public:
    // Spawns argv as a new debug-adapter process. eventLoop must outlive
    // this Client (see FramedConnection.h's own lifetime contract).
    Client(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop);

    // Takes ownership of an already-open Transport directly — for tests
    // driving a raw pipe pair with no real subprocess involved.
    Client(lsp::Transport transport, ned::ui::EventLoop& eventLoop);

    ~Client() = default; // connection_'s own destructor does the real teardown work — see FramedConnection.h

    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&)                 = delete;
    Client& operator=(Client&&)      = delete;

    // Sends {"seq": <fresh>, "type": "request", "command": command,
    // "arguments": arguments}. callback runs on the main thread once the
    // matching response (by "request_seq") arrives; dropped uninvoked if
    // this Client is destroyed first.
    void SendRequest(const std::string& command, Json arguments, ResponseCallback callback);

    // Replaces any existing handler for event (e.g. "stopped",
    // "terminated"). Invoked on the main thread with the event's "body" (an
    // empty object if the adapter sent none).
    void SetEventHandler(std::string event, EventHandler handler);

    // Invoked exactly once, on the main thread, when the connection stops
    // running for any reason.
    void SetOnDisconnected(std::function<void(std::string reason)> handler);

    // Public primarily for tests: FramedConnection's own SetOnFrame callback
    // reaches this via EventLoop::Post; calling it directly exercises the
    // same correlation/dispatch logic without needing a running Run() loop.
    void DispatchFrame(const std::string& frameText);

    // subprocess-hang-protection follow-up -- see
    // Lsp::Client::ExpireStaleRequests's identical doc comment; DAP has no
    // BackgroundActivity spinner to pair, so this is otherwise the same
    // shape (synthetic failure via the existing success=false/message
    // callback branch, no new handling needed at any call site).
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = ProtocolRequestTimeoutMs());

    // async-write-queue follow-up: see FramedConnection::PrepareForGracefulShutdown's
    // doc comment -- call this immediately before a best-effort courtesy
    // request (e.g. Manager::StopSession's "disconnect") that must actually
    // reach the wire before this Client is destroyed.
    void PrepareForGracefulShutdown();

  private:
    protocol::FramedConnection<lsp::Transport> connection_;

    struct PendingRequest {
        ResponseCallback                      callback;
        std::chrono::steady_clock::time_point sentAt;
    };

    int                                            nextSeq_ = 1;
    std::unordered_map<int, PendingRequest>       pending_; // keyed by the request's own seq
    std::unordered_map<std::string, EventHandler> eventHandlers_;
    std::function<void(std::string reason)>       onDisconnected_;
};

} // namespace ned::editor::dap

#endif // NED_EDITOR_DAP_CLIENT_H
