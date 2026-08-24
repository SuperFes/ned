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
// ned::ui::EventLoop::Post; destroy only after EventLoop::Run() has
// returned; transport_ declared after readThread_ so its destructor closes
// the fds that unblock the read thread's blocking ReadFrame) — see
// LspClient.h's own header comment for the full reasoning behind each;
// none of it is repeated here because none of it differs.
//

#ifndef NED_EDITOR_DAP_DAPCLIENT_H
#define NED_EDITOR_DAP_DAPCLIENT_H

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

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

// subprocess-hang-protection follow-up -- see Lsp/LspClient.h's identical
// kDefaultRequestTimeout constant/reasoning; DAP requests (evaluate, a
// stepping command against a busy debuggee, ...) get the same generous
// margin.
inline constexpr std::chrono::milliseconds kDefaultRequestTimeout{30000};

class DapClient {
  public:
    // Spawns argv as a new debug-adapter process. eventLoop must outlive
    // this DapClient (see header comment).
    DapClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop);

    // Takes ownership of an already-open Transport directly — for tests
    // driving a raw pipe pair with no real subprocess involved, mirroring
    // LspClient's own test constructor.
    DapClient(lsp::Transport transport, ned::ui::EventLoop& eventLoop);

    ~DapClient() = default; // member destruction order does the real work — see LspClient.h

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
    // call site).
    void ExpireStaleRequests(std::chrono::milliseconds maxAge = kDefaultRequestTimeout);

  private:
    void StartReadLoop();

    std::jthread   readThread_; // declared before transport_ — see LspClient.h
    lsp::Transport transport_;

    ned::ui::EventLoop& eventLoop_;

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
