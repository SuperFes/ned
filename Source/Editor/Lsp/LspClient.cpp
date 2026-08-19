#include "LspClient.h"

#include <utility>

#include <ftxui/component/animation.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace ned::editor::lsp {

LspClient::LspClient(std::vector<std::string> argv, ftxui::ScreenInteractive& screen) : transport_(std::move(argv)), screen_(screen) {
    StartReadLoop();
}

LspClient::LspClient(Transport transport, ftxui::ScreenInteractive& screen) : transport_(std::move(transport)), screen_(screen) {
    StartReadLoop();
}

void LspClient::StartReadLoop() {
    // transport_ is already fully constructed by the time this runs (called
    // from the constructor *body*, after the member-initializer-list has
    // run) -- see LspClient.h's header comment for why readThread_ has to
    // start out empty (default-constructed) rather than being given real
    // work directly in the initializer list.
    readThread_ = std::jthread([this](std::stop_token) {
        while (true) {
            std::optional<std::string> frame;
            try {
                frame = transport_.ReadFrame(); // blocks
            }
            catch (const std::exception&) {
                // malformed frame -- stop this connection's read loop
                // rather than looping on a corrupt stream. error-visibility
                // follow-up: previously a silent return; now reported via
                // onDisconnected_, same Post-marshaling reasoning as the
                // real-frame case below.
                screen_.Post([this] {
                    if (onDisconnected_) {
                        onDisconnected_("malformed frame from server");
                    }
                    ftxui::animation::RequestAnimationFrame();
                });
                return;
            }
            if (!frame) {
                // EOF -- server exited (or this LspClient is being
                // destroyed, see header comment). error-visibility
                // follow-up: previously silent -- now reported the same way
                // as the malformed-frame case above. A disconnect during
                // this LspClient's own destruction is a real possibility
                // (Transport's destructor closing this end's fds is exactly
                // what makes the blocking ReadFrame() call above finally
                // return -- see header comment); that's safe here for the
                // same reason DispatchFrame's own Post callback already is:
                // this LspClient (and thus onDisconnected_) is never
                // destroyed while ScreenInteractive::Loop() might still
                // process a pending Post, so this callback either runs
                // before destruction starts or never runs at all.
                screen_.Post([this] {
                    if (onDisconnected_) {
                        onDisconnected_("server exited (EOF)");
                    }
                    ftxui::animation::RequestAnimationFrame();
                });
                return;
            }
            // repaint-on-background-update follow-up: ScreenInteractive::
            // Post's own Closure task type never marks the frame dirty by
            // itself (confirmed by reading FTXUI's real app.cpp, not
            // assumed -- its HandleTask's Closure branch just runs the
            // closure and returns, unlike its Event/AnimationTask branches,
            // both of which explicitly set frame_valid_ = false afterward)
            // -- without this, a diagnostic/hover/completion/code-action
            // response arriving here updates real state (e.g.
            // Buffer::SetDiagnostics) but the terminal visibly doesn't
            // change until the next real keystroke or mouse event happens
            // to repaint it anyway, which reads exactly like "nothing
            // happened" even though it did. RequestAnimationFrame() is the
            // same "force a real frame soon, with no dedicated event"
            // mechanism ScrollArrowButton's own repeat/ghost-completion's
            // debounce already rely on for an identical reason.
            screen_.Post([this, frameText = std::move(*frame)]() mutable {
                DispatchFrame(frameText);
                ftxui::animation::RequestAnimationFrame();
            });
        }
    });
}

void LspClient::DispatchFrame(const std::string& frameText) {
    Json message;
    try {
        message = Json::parse(frameText);
    }
    catch (const std::exception&) {
        return; // malformed JSON from the server -- ignore rather than crash the editor
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
        // A notification (no "id") or a server-initiated request (has an
        // "id" too, expecting a response) -- this slice only handles
        // notifications; a server-initiated request is looked up the same
        // way but simply never gets a response, since no server capability
        // this client currently declares would ever prompt one.
        const auto it = notificationHandlers_.find(message["method"].get<std::string>());
        if (it != notificationHandlers_.end() && it->second) {
            it->second(message.contains("params") ? message["params"] : Json::object());
        }
    }
}

void LspClient::SendRequest(const std::string& method, Json params, ResponseCallback callback) {
    const int id       = nextRequestId_++;
    pending_[id]       = std::move(callback);
    const Json message = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)},
    };
    transport_.WriteFrame(message.dump());
}

void LspClient::SendNotification(const std::string& method, Json params) {
    const Json message = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", std::move(params)},
    };
    transport_.WriteFrame(message.dump());
}

void LspClient::SetNotificationHandler(std::string method, NotificationHandler handler) {
    notificationHandlers_[std::move(method)] = std::move(handler);
}

void LspClient::SetOnDisconnected(std::function<void(std::string reason)> handler) {
    onDisconnected_ = std::move(handler);
}

} // namespace ned::editor::lsp
