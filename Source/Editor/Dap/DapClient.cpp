#include "DapClient.h"

#include <cerrno>
#include <utility>

#include <unistd.h>

#include "Editor/DiagnosticsLog.h"

namespace ned::editor::dap {

DapClient::DapClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop)
    : transport_(std::move(argv), /*captureStderr=*/true), eventLoop_(eventLoop) {
    StartReadLoop();
    StartStderrReadLoop();
}

DapClient::DapClient(lsp::Transport transport, ned::ui::EventLoop& eventLoop) : transport_(std::move(transport)), eventLoop_(eventLoop) {
    StartReadLoop();
    StartStderrReadLoop(); // no-op unless transport_ was itself constructed with captureStderr -- see header comment
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
            catch (const std::exception& e) {
                // Malformed frame, or (subprocess-hang-protection follow-up)
                // a mid-frame stall -- see LspClient.cpp's identical comment.
                eventLoop_.Post([this, reason = std::string(e.what())] {
                    LogMessage(LogCategory::Dap, LogSeverity::Warning, reason);
                    if (onDisconnected_) {
                        onDisconnected_(reason);
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

void DapClient::StartStderrReadLoop() {
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
                return; // EOF -- adapter exited, or this DapClient is being destroyed
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
                    LogMessage(LogCategory::Dap, LogSeverity::Warning, label.empty() ? line : label + ": " + line);
                });
            }
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
        ResponseCallback callback = std::move(it->second.callback);
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
    pending_[seq]      = PendingRequest{std::move(callback), std::chrono::steady_clock::now()};
    const Json message = {
        {"seq", seq},
        {"type", "request"},
        {"command", command},
        {"arguments", std::move(arguments)},
    };
    transport_.WriteFrame(message.dump());
}

void DapClient::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    // subprocess-hang-protection follow-up -- see LspClient::ExpireStaleRequests's
    // identical reasoning/collect-then-invoke shape.
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
    for (auto& [seq, callback] : expired) {
        LogMessage(LogCategory::Dap, LogSeverity::Warning, "request " + std::to_string(seq) + " timed out waiting for a response");
        if (callback) {
            callback(false, Json::object(), "ned: request timed out waiting for a response");
        }
    }
}

void DapClient::SetEventHandler(std::string event, EventHandler handler) {
    eventHandlers_[std::move(event)] = std::move(handler);
}

void DapClient::SetOnDisconnected(std::function<void(std::string reason)> handler) {
    onDisconnected_ = std::move(handler);
}

} // namespace ned::editor::dap
