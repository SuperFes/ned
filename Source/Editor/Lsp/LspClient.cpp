#include "LspClient.h"

#include <utility>

#include "Editor/BackgroundActivity.h"
#include "Editor/DiagnosticsLog.h"

namespace ned::editor::lsp {

namespace {

    // background-activity-spinner follow-up: the registry API takes a
    // std::string; build it from the shared string_view constant once.
    const std::string kLspActivity{kLspActivityName};

} // namespace

LspClient::~LspClient() {
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        EndBackgroundActivity(kLspActivity); // see the header's destructor comment
    }
}

LspClient::LspClient(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop)
    : transport_(std::move(argv)), eventLoop_(eventLoop), handshakeComplete_(false) {
    StartReadLoop();
}

LspClient::LspClient(Transport transport, ned::ui::EventLoop& eventLoop, bool startHandshakeComplete)
    : transport_(std::move(transport)), eventLoop_(eventLoop), handshakeComplete_(startHandshakeComplete) {
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
                eventLoop_.Post([this] {
                    LogMessage(LogCategory::Lsp, LogSeverity::Warning, "malformed frame from server");
                    if (onDisconnected_) {
                        onDisconnected_("malformed frame from server");
                    }
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
                // destroyed while EventLoop::Run() might still process a
                // pending Post, so this callback either runs before
                // destruction starts or never runs at all.
                eventLoop_.Post([this] {
                    LogMessage(LogCategory::Lsp, LogSeverity::Warning, "server exited (EOF)");
                    if (onDisconnected_) {
                        onDisconnected_("server exited (EOF)");
                    }
                });
                return;
            }
            // repaint-on-background-update follow-up: under FTXUI,
            // ScreenInteractive::Post's own Closure task type never marked
            // the frame dirty by itself, so a diagnostic/hover/completion/
            // code-action response arriving here updated real state (e.g.
            // Buffer::SetDiagnostics) with no repaint to actually show it
            // until the next real keystroke or mouse event happened to
            // repaint anyway -- fixed there with an explicit
            // RequestAnimationFrame() after every such Post. FTXUI ->
            // Notcurses migration: ned::ui::EventLoop::Run's own loop
            // doesn't have this gap at all -- draining any Post()ed work
            // unconditionally earns the next iteration a repaint (see
            // EventLoop.cpp's own comment on needsRepaint), so no
            // equivalent "force a frame" call is needed here anymore.
            eventLoop_.Post([this, frameText = std::move(*frame)]() mutable { DispatchFrame(frameText); });
        }
    });
}

void LspClient::DispatchFrame(const std::string& frameText) {
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
        ResponseCallback callback = std::move(it->second);
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
            transport_.WriteFrame(response.dump());
            return;
        }

        const auto it = notificationHandlers_.find(method);
        if (it != notificationHandlers_.end() && it->second) {
            it->second(params);
        }
    }
}

void LspClient::SendRequest(const std::string& method, Json params, ResponseCallback callback) {
    // handshake-ordering follow-up: see this method's own doc comment.
    // "initialize" itself is exempt -- it's what starts the handshake.
    if (!handshakeComplete_ && method != "initialize") {
        pendingUntilHandshake_.emplace_back([this, method, params = std::move(params), callback = std::move(callback)]() mutable {
            SendRequest(method, std::move(params), std::move(callback));
        });
        return;
    }

    const int id = nextRequestId_++;
    pending_[id] = std::move(callback);
    BeginBackgroundActivity(kLspActivity); // ended when the response dispatches, or by ~LspClient for a request never answered
    const Json message = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(params)},
    };
    transport_.WriteFrame(message.dump());
}

void LspClient::SendNotification(const std::string& method, Json params) {
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
    transport_.WriteFrame(message.dump());

    if (method == "initialized") {
        handshakeComplete_                              = true;
        const std::vector<std::function<void()>> queued = std::move(pendingUntilHandshake_);
        for (const std::function<void()>& thunk : queued) {
            thunk(); // replays each queued SendRequest/SendNotification in the order it was originally called
        }
    }
}

void LspClient::SetNotificationHandler(std::string method, NotificationHandler handler) {
    notificationHandlers_[std::move(method)] = std::move(handler);
}

void LspClient::SetRequestHandler(std::string method, RequestHandler handler) {
    requestHandlers_[std::move(method)] = std::move(handler);
}

void LspClient::SetOnDisconnected(std::function<void(std::string reason)> handler) {
    onDisconnected_ = std::move(handler);
}

} // namespace ned::editor::lsp
