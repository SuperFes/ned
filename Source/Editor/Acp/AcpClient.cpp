#include "AcpClient.h"

#include <utility>

namespace ned::editor::acp {

AcpClient::AcpClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop) : transport_(std::move(argv)), eventLoop_(eventLoop) {
    StartReadLoop();
}

AcpClient::AcpClient(Transport transport, ned::ui::EventLoop& eventLoop) : transport_(std::move(transport)), eventLoop_(eventLoop) {
    StartReadLoop();
}

void AcpClient::StartReadLoop() {
    // transport_ is already fully constructed by the time this runs (called
    // from the constructor *body*) -- see LspClient.cpp's identical comment
    // for why readThread_ has to start out empty rather than being given
    // real work directly in the initializer list.
    readThread_ = std::jthread([this](std::stop_token) {
        while (true) {
            std::optional<std::string> message;
            try {
                message = transport_.ReadMessage(); // blocks
            }
            catch (const std::exception&) {
                eventLoop_.Post([this] {
                    if (onDisconnected_) {
                        onDisconnected_("malformed message from agent");
                    }
                });
                return;
            }
            if (!message) {
                // EOF -- agent exited (or this AcpClient is being destroyed,
                // see header comment: Transport's destructor closing this
                // end's fds is exactly what makes the blocking ReadMessage()
                // call above finally return). Safe the same way LspClient's
                // own equivalent Post is: this AcpClient is never destroyed
                // while EventLoop::Run() might still process a pending Post,
                // so this callback either runs before destruction starts or
                // never runs at all.
                eventLoop_.Post([this] {
                    if (onDisconnected_) {
                        onDisconnected_("agent exited (EOF)");
                    }
                });
                return;
            }
            if (message->empty()) {
                continue; // a bare blank line -- see Transport::ReadMessage's own doc comment
            }
            eventLoop_.Post([this, frameText = std::move(*message)]() mutable { DispatchFrame(frameText); });
        }
    });
}

void AcpClient::DispatchFrame(const std::string& frameText) {
    Json message;
    try {
        message = Json::parse(frameText);
    }
    catch (const std::exception&) {
        return; // malformed JSON from the agent -- ignore rather than crash the editor
    }

    if (message.contains("id") && (message.contains("result") || message.contains("error"))) {
        const auto it = pending_.find(message["id"].get<int>());
        if (it == pending_.end()) {
            return; // unknown/already-handled id -- ignore
        }
        ResponseCallback callback = std::move(it->second);
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
                transport_.WriteMessage(response.dump());
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
                transport_.WriteMessage(response.dump());
            });
            return;
        }

        const auto it = notificationHandlers_.find(method);
        if (it != notificationHandlers_.end() && it->second) {
            it->second(params);
        }
    }
}

void AcpClient::SendRequest(const std::string& method, Json params, ResponseCallback callback) {
    const int id       = nextRequestId_++;
    pending_[id]       = std::move(callback);
    const Json message = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)},
    };
    transport_.WriteMessage(message.dump());
}

void AcpClient::SendNotification(const std::string& method, Json params) {
    const Json message = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", std::move(params)},
    };
    transport_.WriteMessage(message.dump());
}

void AcpClient::SetNotificationHandler(std::string method, NotificationHandler handler) {
    notificationHandlers_[std::move(method)] = std::move(handler);
}

void AcpClient::SetRequestHandler(std::string method, RequestHandler handler) {
    requestHandlers_[std::move(method)] = std::move(handler);
}

void AcpClient::SetOnDisconnected(std::function<void(std::string reason)> handler) {
    onDisconnected_ = std::move(handler);
}

} // namespace ned::editor::acp
