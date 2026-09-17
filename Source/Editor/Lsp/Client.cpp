#include "Client.h"

#include <utility>

#include "Editor/BackgroundActivity.h"
#include "Editor/DiagnosticsLog.h"

namespace ned::editor::lsp {

namespace {

    // background-activity-spinner follow-up: the registry API takes a
    // std::string; build it from the shared string_view constant once.
    const std::string kLspActivity{kLspActivityName};

    protocol::FramedConnection<Transport>::Options MakeConnectionOptions() {
        return {LogCategory::Lsp, "server"};
    }

} // namespace

Client::~Client() {
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        EndBackgroundActivity(kLspActivity); // see the header's destructor comment
    }
}

Client::Client(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop) : connection_(std::move(argv), /*captureStderr=*/true, eventLoop, MakeConnectionOptions()), handshakeComplete_(false) {
    connection_.SetOnFrame([this](std::string frame) { DispatchFrame(frame); });
    connection_.SetOnDisconnected([this](std::string reason) {
        LogMessage(LogCategory::Lsp, LogSeverity::Warning, reason);
        if (onDisconnected_) {
            onDisconnected_(reason);
        }
    });
}

Client::Client(Transport transport, ned::ui::EventLoop& eventLoop, bool startHandshakeComplete) : connection_(std::move(transport), eventLoop, MakeConnectionOptions()), handshakeComplete_(startHandshakeComplete) {
    connection_.SetOnFrame([this](std::string frame) { DispatchFrame(frame); });
    connection_.SetOnDisconnected([this](std::string reason) {
        LogMessage(LogCategory::Lsp, LogSeverity::Warning, reason);
        if (onDisconnected_) {
            onDisconnected_(reason);
        }
    });
}

void Client::PrepareForGracefulShutdown() {
    connection_.PrepareForGracefulShutdown();
}

void Client::DispatchFrame(const std::string& frameText) {
    Json message;
    try {
        message = Json::parse(frameText);
    }
    catch (const std::exception& e) {
        // malformed JSON from the server -- ignore rather than crash the
        // editor, but no longer silently: diagnostics-log follow-up.
        LogMessage(LogCategory::Lsp, LogSeverity::Warning, std::string("malformed JSON frame: ") + e.what());
        return;
    }

    if (message.contains("id") && (message.contains("result") || message.contains("error"))) {
        const auto it = pending_.find(message["id"].get<int>());
        if (it == pending_.end()) {
            return; // unknown/already-handled id -- ignore
        }
        ResponseCallback callback = std::move(it->second.callback);
        pending_.erase(it);
        EndBackgroundActivity(kLspActivity); // pairs with SendRequest's Begin
        if (callback) {
            if (message.contains("error")) {
                callback(std::nullopt, message["error"]);
            }
            else {
                callback(message["result"], std::nullopt);
            }
        }
        return;
    }

    if (message.contains("method")) {
        const std::string method = message["method"].get<std::string>();
        const Json        params = message.contains("params") ? message["params"] : Json::object();

        // workDoneProgress-support follow-up: a server-initiated *request*
        // (has an "id" too, expects a response) now gets one --
        // "window/workDoneProgress/create" is the first such request any
        // declared capability can prompt, and leaving it unanswered stalls a
        // spec-following server's progress reporting. No handler means a
        // MethodNotFound error response per JSON-RPC, not silence.
        if (message.contains("id")) {
            const auto it = requestHandlers_.find(method);
            Json       response;
            if (it != requestHandlers_.end() && it->second) {
                response = Json{{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", it->second(params)}};
            }
            else {
                response = Json{{"jsonrpc", "2.0"},
                                {"id", message["id"]},
                                {"error", {{"code", -32601}, {"message", "method not found: " + method}}}};
            }
            connection_.SendFrame(response.dump());
            return;
        }

        const auto it = notificationHandlers_.find(method);
        if (it != notificationHandlers_.end() && it->second) {
            it->second(params);
        }
    }
}

void Client::SendRequest(const std::string& method, Json params, ResponseCallback callback) {
    // handshake-ordering follow-up: see this method's own doc comment.
    // "initialize" itself is exempt -- it's what starts the handshake.
    if (!handshakeComplete_ && method != "initialize") {
        pendingUntilHandshake_.emplace_back([this, method, params = std::move(params), callback = std::move(callback)]() mutable {
            SendRequest(method, std::move(params), std::move(callback));
        });
        return;
    }

    const int id = nextRequestId_++;
    pending_[id] = PendingRequest{std::move(callback), std::chrono::steady_clock::now()};
    BeginBackgroundActivity(kLspActivity); // ended when the response dispatches, or by ~Client for a request never answered
    const Json message = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)},
    };
    connection_.SendFrame(message.dump());
}

void Client::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    // subprocess-hang-protection follow-up. Collect first, then erase+invoke
    // -- a callback could in principle call back into this Client
    // (SendRequest again, say), which must not happen while iterating
    // pending_ itself.
    const std::chrono::steady_clock::time_point   now = std::chrono::steady_clock::now();
    std::vector<std::pair<int, ResponseCallback>> expired;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->second.sentAt >= maxAge) {
            expired.emplace_back(it->first, std::move(it->second.callback));
            it = pending_.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto& [id, callback] : expired) {
        EndBackgroundActivity(kLspActivity); // pairs with SendRequest's Begin
        LogMessage(LogCategory::Lsp, LogSeverity::Warning, "request " + std::to_string(id) + " timed out waiting for a response");
        if (callback) {
            callback(std::nullopt, Json{{"code", -32001}, {"message", "ned: request timed out waiting for a response"}});
        }
    }
}

void Client::SendNotification(const std::string& method, Json params) {
    // handshake-ordering follow-up: see this method's own doc comment.
    // "initialized" itself is exempt -- it's what opens the gate below.
    if (!handshakeComplete_ && method != "initialized") {
        pendingUntilHandshake_.emplace_back(
            [this, method, params = std::move(params)]() mutable { SendNotification(method, std::move(params)); });
        return;
    }

    const Json message = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", std::move(params)},
    };
    connection_.SendFrame(message.dump());

    if (method == "initialized") {
        handshakeComplete_                              = true;
        const std::vector<std::function<void()>> queued = std::move(pendingUntilHandshake_);
        for (const std::function<void()>& thunk : queued) {
            thunk(); // replays each queued SendRequest/SendNotification in the order it was originally called
        }
    }
}

void Client::SetNotificationHandler(std::string method, NotificationHandler handler) {
    notificationHandlers_[std::move(method)] = std::move(handler);
}

void Client::SetRequestHandler(std::string method, RequestHandler handler) {
    requestHandlers_[std::move(method)] = std::move(handler);
}

void Client::SetOnDisconnected(std::function<void(std::string reason)> handler) {
    onDisconnected_ = std::move(handler);
}

} // namespace ned::editor::lsp
