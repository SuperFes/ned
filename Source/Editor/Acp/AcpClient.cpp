#include "AcpClient.h"

#include <cerrno>
#include <utility>

#include <unistd.h>

#include "Editor/DiagnosticsLog.h"

namespace ned::editor::acp {

AcpClient::AcpClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop)
    : transport_(std::move(argv), /*captureStderr=*/true), eventLoop_(eventLoop) {
    StartReadLoop();
    StartStderrReadLoop();
}

AcpClient::AcpClient(Transport transport, ned::ui::EventLoop& eventLoop) : transport_(std::move(transport)), eventLoop_(eventLoop) {
    StartReadLoop();
    StartStderrReadLoop(); // no-op unless transport_ was itself constructed with captureStderr -- see header comment
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
            catch (const std::exception& e) {
                // Malformed message, or (subprocess-hang-protection
                // follow-up) a mid-message stall -- see LspClient.cpp's
                // identical comment.
                eventLoop_.Post([this, reason = std::string(e.what())] {
                    LogMessage(LogCategory::Acp, LogSeverity::Warning, reason);
                    if (onDisconnected_) {
                        onDisconnected_(reason);
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
                    LogMessage(LogCategory::Acp, LogSeverity::Warning, "agent exited (EOF)");
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

void AcpClient::StartStderrReadLoop() {
    // Identical to LspClient::StartStderrReadLoop -- see that function's own
    // doc comment for the full reasoning; only the log category differs.
    const int fd = transport_.StderrFd();
    if (fd < 0) {
        return; // not captured -- see header comment
    }
    std::string label = transport_.ProcessLabel();

    stderrThread_ = std::jthread([this, fd, label = std::move(label)](std::stop_token) {
        std::string buffered;
        char        chunk[4096];
        while (true) {
            const ssize_t result = ::read(fd, chunk, sizeof(chunk));
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return;
            }
            if (result == 0) {
                return; // EOF -- agent exited, or this AcpClient is being destroyed
            }
            buffered.append(chunk, static_cast<std::size_t>(result));

            std::size_t newline;
            while ((newline = buffered.find('\n')) != std::string::npos) {
                std::string line = buffered.substr(0, newline);
                buffered.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    continue;
                }
                eventLoop_.Post([this, label, line = std::move(line)] {
                    LogMessage(LogCategory::Acp, LogSeverity::Warning, label.empty() ? line : label + ": " + line);
                });
            }
        }
    });
}

void AcpClient::DispatchFrame(const std::string& frameText) {
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
    pending_[id]       = PendingRequest{std::move(callback), std::chrono::steady_clock::now()};
    const Json message = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)},
    };
    transport_.WriteMessage(message.dump());
}

void AcpClient::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    // subprocess-hang-protection follow-up -- see LspClient::ExpireStaleRequests's
    // identical reasoning/collect-then-invoke shape.
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
