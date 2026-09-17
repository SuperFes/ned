#include "Client.h"

#include <utility>

#include "Editor/DiagnosticsLog.h"

namespace ned::editor::acp {

namespace {

    protocol::FramedConnection<Transport>::Options MakeConnectionOptions() {
        // acp-stderr-severity follow-up: Info, not Warning -- a real agent's
        // stderr is routinely just informational chatter (a startup banner, a
        // "session started" line), not necessarily a warning. Unlike Lsp
        // (which defaults to a hidden log category specifically because its
        // stderr is noisy), Acp defaults visible, so a Warning-severity entry
        // here tripped BufferView's unsolicited "New warning -- see
        // *Messages*" echo message for completely benign agent startup text
        // -- reported live. Genuine ACP-level problems (agent exited,
        // malformed frame, request timeout, a real disconnect) are logged at
        // Warning from their own call sites in DispatchFrame/SendRequest and
        // are unaffected.
        return {LogCategory::Acp, "agent", LogSeverity::Info};
    }

} // namespace

Client::Client(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop) : connection_(std::move(argv), /*captureStderr=*/true, eventLoop, MakeConnectionOptions()) {
    connection_.SetOnFrame([this](std::string frame) { DispatchFrame(frame); });
    connection_.SetOnDisconnected([this](std::string reason) {
        if (onDisconnected_) {
            onDisconnected_(reason);
        }
    });
}

Client::Client(Transport transport, ned::ui::EventLoop& eventLoop) : connection_(std::move(transport), eventLoop, MakeConnectionOptions()) {
    connection_.SetOnFrame([this](std::string frame) { DispatchFrame(frame); });
    connection_.SetOnDisconnected([this](std::string reason) {
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
        // malformed JSON from the agent -- ignore rather than crash the
        // editor, but no longer silently: diagnostics-log follow-up.
        LogMessage(LogCategory::Acp, LogSeverity::Warning, std::string("malformed JSON frame: ") + e.what());
        return;
    }

    // Any well-formed frame at all -- a response, a notification, an
    // agent-initiated request -- is proof the agent is alive and working.
    // See ExpireStaleRequests's doc comment for why this matters: a
    // long-running tool call that streams tool_call_update chatter must not
    // trip the same stale-request timeout a genuinely hung connection would.
    lastActivityAt_ = std::chrono::steady_clock::now();

    if (message.contains("id") && (message.contains("result") || message.contains("error"))) {
        const auto it = pending_.find(message["id"].get<int>());
        if (it == pending_.end()) {
            return; // unknown/already-handled id -- ignore
        }
        ResponseCallback callback = std::move(it->second.callback);
        pending_.erase(it);
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

        if (message.contains("id")) {
            const Json requestId = message["id"];
            const auto it        = requestHandlers_.find(method);
            if (it == requestHandlers_.end() || !it->second) {
                const Json response = {{"jsonrpc", "2.0"},
                                       {"id", requestId},
                                       {"error", {{"code", -32601}, {"message", "method not found: " + method}}}};
                connection_.SendFrame(response.dump());
                return;
            }
            it->second(params, [this, requestId](std::optional<Json> result, std::optional<Json> error) {
                Json response = {{"jsonrpc", "2.0"}, {"id", requestId}};
                if (error) {
                    response["error"] = *error;
                }
                else {
                    response["result"] = result.value_or(Json(nullptr));
                }
                connection_.SendFrame(response.dump());
            });
            return;
        }

        const auto it = notificationHandlers_.find(method);
        if (it != notificationHandlers_.end() && it->second) {
            it->second(params);
        }
    }
}

void Client::SendRequest(const std::string& method, Json params, ResponseCallback callback) {
    const int id       = nextRequestId_++;
    pending_[id]       = PendingRequest{std::move(callback), std::chrono::steady_clock::now()};
    const Json message = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)},
    };
    connection_.SendFrame(message.dump());
}

void Client::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    // subprocess-hang-protection follow-up -- see
    // Lsp::Client::ExpireStaleRequests's identical reasoning/collect-then-
    // invoke shape.
    const std::chrono::steady_clock::time_point now    = std::chrono::steady_clock::now();
    const std::chrono::steady_clock::time_point cutoff = now - maxAge;
    // A request is only truly stale if it's old AND nothing at all has been
    // heard from the agent recently either -- see this method's doc comment.
    if (lastActivityAt_ > cutoff) {
        return;
    }
    std::vector<std::pair<int, ResponseCallback>> expired;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.sentAt <= cutoff) {
            expired.emplace_back(it->first, std::move(it->second.callback));
            it = pending_.erase(it);
        }
        else {
            ++it;
        }
    }
    for (auto& [id, callback] : expired) {
        LogMessage(LogCategory::Acp, LogSeverity::Warning, "request " + std::to_string(id) + " timed out waiting for a response");
        if (callback) {
            callback(std::nullopt, Json{{"code", -32001}, {"message", "ned: request timed out waiting for a response"}});
        }
    }
}

void Client::SendNotification(const std::string& method, Json params) {
    const Json message = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", std::move(params)},
    };
    connection_.SendFrame(message.dump());
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

} // namespace ned::editor::acp
