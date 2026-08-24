#include "DapClient.h"

#include <utility>

#include "Editor/DiagnosticsLog.h"

namespace ned::editor::dap {

DapClient::DapClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop) : transport_(std::move(argv)), eventLoop_(eventLoop) {
    StartReadLoop();
}

DapClient::DapClient(lsp::Transport transport, ned::ui::EventLoop& eventLoop) : transport_(std::move(transport)), eventLoop_(eventLoop) {
    StartReadLoop();
}

void DapClient::StartReadLoop() {
    // Identical loop to LspClient::StartReadLoop — see that function (and
    // LspClient.h's header comment) for the reasoning behind every branch;
    // only the dispatch target differs.
    readThread_ = std::jthread([this](std::stop_token) {
        while (true) {
            std::optional<std::string> frame;
            try {
                frame = transport_.ReadFrame(); // blocks
            }
            catch (const std::exception&) {
                eventLoop_.Post([this] {
                    LogMessage(LogCategory::Dap, LogSeverity::Warning, "malformed frame from adapter");
                    if (onDisconnected_) {
                        onDisconnected_("malformed frame from adapter");
                    }
                });
                return;
            }
            if (!frame) {
                eventLoop_.Post([this] {
                    LogMessage(LogCategory::Dap, LogSeverity::Warning, "adapter exited (EOF)");
                    if (onDisconnected_) {
                        onDisconnected_("adapter exited (EOF)");
                    }
                });
                return;
            }
            eventLoop_.Post([this, frameText = std::move(*frame)]() mutable { DispatchFrame(frameText); });
        }
    });
}

void DapClient::DispatchFrame(const std::string& frameText) {
    Json message;
    try {
        message = Json::parse(frameText);
    }
    catch (const std::exception& e) {
        // malformed JSON from the adapter — ignore rather than crash the
        // editor, but no longer silently: diagnostics-log follow-up.
        LogMessage(LogCategory::Dap, LogSeverity::Warning, std::string("malformed JSON frame: ") + e.what());
        return;
    }

    const std::string type = message.value("type", "");

    if (type == "response") {
        if (!message.contains("request_seq") || !message["request_seq"].is_number_integer()) {
            return;
        }
        const auto it = pending_.find(message["request_seq"].get<int>());
        if (it == pending_.end()) {
            return; // unknown/already-handled request_seq — ignore
        }
        ResponseCallback callback = std::move(it->second);
        pending_.erase(it);
        if (callback) {
            const bool success = message.value("success", false);
            if (success) {
                callback(true, message.contains("body") ? message["body"] : Json::object(), "");
            }
            else {
                callback(false, Json::object(), message.value("message", "request failed"));
            }
        }
        return;
    }

    if (type == "event") {
        const auto it = eventHandlers_.find(message.value("event", ""));
        if (it != eventHandlers_.end() && it->second) {
            it->second(message.contains("body") ? message["body"] : Json::object());
        }
    }
    // A "request" from the adapter (reverse request, e.g. runInTerminal) is
    // deliberately ignored in this slice — the initialize request declares
    // supportsRunInTerminalRequest: false, so a conforming adapter never
    // sends the only reverse request that matters here.
}

void DapClient::SendRequest(const std::string& command, Json arguments, ResponseCallback callback) {
    const int seq      = nextSeq_++;
    pending_[seq]      = std::move(callback);
    const Json message = {
        {"seq", seq},
        {"type", "request"},
        {"command", command},
        {"arguments", std::move(arguments)},
    };
    transport_.WriteFrame(message.dump());
}

void DapClient::SetEventHandler(std::string event, EventHandler handler) {
    eventHandlers_[std::move(event)] = std::move(handler);
}

void DapClient::SetOnDisconnected(std::function<void(std::string reason)> handler) {
    onDisconnected_ = std::move(handler);
}

} // namespace ned::editor::dap
