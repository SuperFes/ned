#include "LspBroker.h"

namespace ned::editor::lsp {

namespace {

    // Builds a plain JSON-RPC 2.0 error response for id -- code -32603
    // (Internal error) covers every failure shape this file synthesizes
    // (spawn failure, real-handshake error); a client receiving this
    // behaves exactly as it would against a real server that rejected
    // initialize, which LspManager.cpp's own initialize callback already
    // tolerates (it unconditionally sends "initialized" regardless of the
    // response's success/error shape -- an existing, unrelated LspManager
    // quirk, not something this file works around).
    Json ErrorResponse(const Json& id, const std::string& message) {
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", Json{{"code", -32603}, {"message", message}}}};
    }

    Json SuccessResponse(const Json& id, const Json& result) {
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

} // namespace

BrokerRouter::BrokerRouter(int maxConcurrentServers) : maxConcurrentServers_(maxConcurrentServers) {
}

std::string BrokerRouter::MakeKey(const std::string& root, const std::string& language) {
    return root + '\x1f' + language;
}

void BrokerRouter::TearDownEntry(const std::string& key, std::vector<BrokerAction>& actions) {
    const auto it = languages_.find(key);
    if (it == languages_.end()) {
        return;
    }
    LanguageState& state = it->second;
    if (state.status == BrokerLanguageStatus::Ready) {
        const int shutdownId = state.nextBrokerId++;
        actions.push_back(BrokerAction{.kind     = BrokerAction::Kind::SendToServer,
                                        .root     = state.root,
                                        .language = state.language,
                                        .frame    = Json{{"jsonrpc", "2.0"}, {"id", shutdownId}, {"method", "shutdown"}}});
        actions.push_back(BrokerAction{
            .kind = BrokerAction::Kind::SendToServer, .root = state.root, .language = state.language, .frame = Json{{"jsonrpc", "2.0"}, {"method", "exit"}}});
    }
    if (state.status != BrokerLanguageStatus::NotStarted) {
        // A real subprocess exists (or was at least requested) for every
        // status but NotStarted -- tell the imperative layer to close and
        // forget it, after the graceful shutdown/exit frames above, if any
        // were sent. Emitted even mid-spawn/mid-handshake (no Ready
        // shutdown/exit possible yet) so a process evicted/disconnected
        // before ever reaching Ready still gets torn down instead of
        // leaking.
        actions.push_back(BrokerAction{.kind = BrokerAction::Kind::CloseServer, .root = state.root, .language = state.language});
    }
    for (ConnectionId conn : state.attached) {
        actions.push_back(BrokerAction{.kind = BrokerAction::Kind::CloseClient, .connection = conn});
        connectionKey_.erase(conn);
    }
    for (const QueuedInitialize& queued : state.pendingInitializeRequests) {
        actions.push_back(BrokerAction{.kind = BrokerAction::Kind::CloseClient, .connection = queued.connection});
        connectionKey_.erase(queued.connection);
    }
    languages_.erase(it);
}

void BrokerRouter::MaybeEvictForCapacity(std::vector<BrokerAction>& actions) {
    if (maxConcurrentServers_ <= 0 || static_cast<int>(languages_.size()) < maxConcurrentServers_) {
        return;
    }
    std::string                           oldestKey;
    std::chrono::steady_clock::time_point oldestTime = std::chrono::steady_clock::time_point::max();
    for (const auto& [key, state] : languages_) {
        if (!state.attached.empty() || !state.pendingInitializeRequests.empty()) {
            continue; // never evict something with a live/queued client
        }
        if (state.lastActive < oldestTime) {
            oldestTime = state.lastActive;
            oldestKey  = key;
        }
    }
    if (!oldestKey.empty()) {
        TearDownEntry(oldestKey, actions);
    }
    // If nothing qualified (every entry busy), the cap is simply exceeded.
}

std::vector<BrokerAction> BrokerRouter::ClientAttached(ConnectionId conn, std::string root, std::string language, std::vector<std::string> argv,
                                                        std::chrono::steady_clock::time_point now) {
    std::vector<BrokerAction> actions;
    const std::string         key = MakeKey(root, language);
    connectionKey_[conn]          = key;

    auto it = languages_.find(key);
    if (it == languages_.end()) {
        MaybeEvictForCapacity(actions);
        LanguageState state;
        state.root       = root;
        state.language    = language;
        state.argv        = std::move(argv);
        state.status       = BrokerLanguageStatus::SpawningProcess;
        state.lastActive  = now;
        actions.push_back(BrokerAction{.kind = BrokerAction::Kind::SpawnServer, .root = root, .language = language, .argv = state.argv});
        languages_.emplace(key, std::move(state));
    }
    else {
        it->second.lastActive = now;
    }
    // Any other status: a later attach's argv is silently ignored -- see
    // this method's own doc comment.
    return actions;
}

void BrokerRouter::MaybeStartRealHandshake(LanguageState& state, std::vector<BrokerAction>& actions) {
    if (state.status != BrokerLanguageStatus::SpawningProcess || !state.processRunning || state.pendingInitializeRequests.empty()) {
        return;
    }
    state.handshakeBrokerId = state.nextBrokerId++;
    Json handshakeParams    = state.pendingInitializeRequests.front().params;
    state.status            = BrokerLanguageStatus::AwaitingRealHandshake;
    actions.push_back(BrokerAction{
        .kind     = BrokerAction::Kind::SendToServer,
        .root     = state.root,
        .language = state.language,
        .frame    = Json{{"jsonrpc", "2.0"}, {"id", state.handshakeBrokerId}, {"method", "initialize"}, {"params", std::move(handshakeParams)}},
    });
}

void BrokerRouter::FlushPendingInitializeRequests(LanguageState& state, std::vector<BrokerAction>& actions) const {
    for (const QueuedInitialize& queued : state.pendingInitializeRequests) {
        if (state.status == BrokerLanguageStatus::Ready) {
            actions.push_back(BrokerAction{
                .kind       = BrokerAction::Kind::SendToClient,
                .frame      = SuccessResponse(queued.originalId, *state.cachedInitializeResult),
                .connection = queued.connection,
            });
        }
        else {
            actions.push_back(BrokerAction{
                .kind       = BrokerAction::Kind::SendToClient,
                .frame      = ErrorResponse(queued.originalId, state.failureReason),
                .connection = queued.connection,
            });
        }
    }
    state.pendingInitializeRequests.clear();
}

std::vector<BrokerAction> BrokerRouter::ClientFrame(ConnectionId conn, const Json& frame, std::chrono::steady_clock::time_point now) {
    std::vector<BrokerAction> actions;
    const auto                keyIt = connectionKey_.find(conn);
    if (keyIt == connectionKey_.end()) {
        return actions; // never attached -- ignore, matches doc comment
    }
    LanguageState& state = languages_[keyIt->second];
    state.lastActive     = now;

    const std::string method = frame.value("method", std::string());
    const bool        hasId  = frame.contains("id");

    if (method == "initialize" && hasId) {
        if (state.status == BrokerLanguageStatus::Ready) {
            actions.push_back(BrokerAction{
                .kind       = BrokerAction::Kind::SendToClient,
                .frame      = SuccessResponse(frame.at("id"), *state.cachedInitializeResult),
                .connection = conn,
            });
        }
        else if (state.status == BrokerLanguageStatus::Failed) {
            actions.push_back(BrokerAction{
                .kind       = BrokerAction::Kind::SendToClient,
                .frame      = ErrorResponse(frame.at("id"), state.failureReason),
                .connection = conn,
            });
        }
        else {
            // SpawningProcess or AwaitingRealHandshake -- queue until the
            // real handshake resolves one way or the other.
            state.pendingInitializeRequests.push_back(QueuedInitialize{
                .connection = conn,
                .originalId = frame.at("id"),
                .params     = frame.value("params", Json::object()),
            });
            MaybeStartRealHandshake(state, actions);
        }
        return actions;
    }

    if (method == "initialized") {
        // Swallowed unconditionally -- the real "initialized" already went
        // out exactly once, when this entry first became Ready (see
        // ServerFrame). Only actually marks the client attached when the
        // entry really is Ready; if it's Failed (this client's own
        // "initialize" already got an error reply, but LspManager.cpp's
        // callback sends "initialized" regardless -- see this file's
        // ErrorResponse comment), there's nothing further for this
        // connection to do here.
        if (state.status == BrokerLanguageStatus::Ready) {
            state.attached.insert(conn);
        }
        return actions;
    }

    // Ordinary post-handshake traffic. Only a client that's actually
    // reached Ready and completed its own "initialized" is allowed through
    // -- anything else (including a Failed entry a client somehow still
    // has open) is treated the same as a disconnected server: close it,
    // letting the editor-side LspManager's own existing crash-recovery path
    // take over on its next SyncBuffer attempt.
    if (state.attached.find(conn) == state.attached.end() || state.status != BrokerLanguageStatus::Ready) {
        actions.push_back(BrokerAction{.kind = BrokerAction::Kind::CloseClient, .connection = conn});
        return actions;
    }

    if (hasId) {
        const int brokerId                = state.nextBrokerId++;
        state.pendingByBrokerId[brokerId] = PendingRequest{.connection = conn, .originalId = frame.at("id")};
        Json rewritten                    = frame;
        rewritten["id"]                   = brokerId;
        actions.push_back(BrokerAction{.kind = BrokerAction::Kind::SendToServer, .root = state.root, .language = state.language, .frame = std::move(rewritten)});
    }
    else {
        actions.push_back(BrokerAction{.kind = BrokerAction::Kind::SendToServer, .root = state.root, .language = state.language, .frame = frame});
    }
    return actions;
}

std::vector<BrokerAction> BrokerRouter::ClientDisconnected(ConnectionId conn) {
    std::vector<BrokerAction> actions; // never emits anything -- see doc comment
    const auto                keyIt = connectionKey_.find(conn);
    if (keyIt == connectionKey_.end()) {
        return actions;
    }
    LanguageState& state = languages_[keyIt->second];
    state.attached.erase(conn);
    std::erase_if(state.pendingInitializeRequests, [conn](const QueuedInitialize& q) { return q.connection == conn; });
    std::erase_if(state.pendingByBrokerId, [conn](const auto& entry) { return entry.second.connection == conn; });
    connectionKey_.erase(keyIt);
    return actions;
}

std::vector<BrokerAction> BrokerRouter::ServerSpawned(const std::string& root, const std::string& language) {
    std::vector<BrokerAction> actions;
    LanguageState&            state = languages_[MakeKey(root, language)];
    state.root                      = root;
    state.language                  = language;
    state.processRunning            = true;
    MaybeStartRealHandshake(state, actions);
    return actions;
}

std::vector<BrokerAction> BrokerRouter::ServerSpawnFailed(const std::string& root, const std::string& language, std::string reason) {
    std::vector<BrokerAction> actions;
    LanguageState&            state = languages_[MakeKey(root, language)];
    state.root                      = root;
    state.language                  = language;
    state.status                    = BrokerLanguageStatus::Failed;
    state.failureReason             = std::move(reason);
    FlushPendingInitializeRequests(state, actions);
    return actions;
}

std::vector<BrokerAction> BrokerRouter::ServerFrame(const std::string& root, const std::string& language, const Json& frame,
                                                     std::chrono::steady_clock::time_point now) {
    std::vector<BrokerAction> actions;
    const std::string         key    = MakeKey(root, language);
    const auto                keyIt  = languages_.find(key);
    if (keyIt == languages_.end()) {
        return actions; // a frame from a server we no longer track (e.g. arrived after ServerDisconnected already reset it) -- ignore
    }
    LanguageState& state = keyIt->second;
    state.lastActive      = now;

    const bool hasId     = frame.contains("id");
    const bool hasMethod = frame.contains("method");

    if (state.status == BrokerLanguageStatus::AwaitingRealHandshake && hasId && frame.at("id").is_number_integer() &&
        frame.at("id").get<int>() == state.handshakeBrokerId) {
        if (frame.contains("result")) {
            state.cachedInitializeResult = frame.at("result");
            state.status                 = BrokerLanguageStatus::Ready;
            actions.push_back(BrokerAction{
                .kind     = BrokerAction::Kind::SendToServer,
                .root     = root,
                .language = language,
                .frame    = Json{{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", Json::object()}},
            });
        }
        else {
            state.status        = BrokerLanguageStatus::Failed;
            state.failureReason = frame.contains("error") ? frame.at("error").value("message", std::string("unknown error")) : "unknown error";
        }
        FlushPendingInitializeRequests(state, actions);
        return actions;
    }

    if (hasId && !hasMethod) {
        // An ordinary response to something we relayed.
        const int brokerId = frame.at("id").is_number_integer() ? frame.at("id").get<int>() : -1;
        if (const auto pendingIt = state.pendingByBrokerId.find(brokerId); pendingIt != state.pendingByBrokerId.end()) {
            Json rewritten  = frame;
            rewritten["id"] = pendingIt->second.originalId;
            actions.push_back(
                BrokerAction{.kind = BrokerAction::Kind::SendToClient, .frame = std::move(rewritten), .connection = pendingIt->second.connection});
            state.pendingByBrokerId.erase(pendingIt);
        }
        return actions;
    }

    if (hasId && hasMethod) {
        // Server-initiated request -- auto-acknowledge, never routed to a
        // client. See this file's own header comment for why.
        actions.push_back(
            BrokerAction{.kind = BrokerAction::Kind::SendToServer, .root = root, .language = language, .frame = SuccessResponse(frame.at("id"), nullptr)});
        return actions;
    }

    // A notification -- broadcast to every attached client.
    for (ConnectionId conn : state.attached) {
        actions.push_back(BrokerAction{.kind = BrokerAction::Kind::SendToClient, .frame = frame, .connection = conn});
    }
    return actions;
}

std::vector<BrokerAction> BrokerRouter::ServerDisconnected(const std::string& root, const std::string& language) {
    std::vector<BrokerAction> actions;
    TearDownEntry(MakeKey(root, language), actions);
    return actions;
}

std::vector<BrokerAction> BrokerRouter::IdleSweep(std::chrono::steady_clock::time_point now, std::chrono::milliseconds idleTimeout) {
    std::vector<BrokerAction> actions;
    std::vector<std::string>  idleKeys;
    for (const auto& [key, state] : languages_) {
        if (!state.attached.empty() || !state.pendingInitializeRequests.empty()) {
            continue;
        }
        if (now - state.lastActive >= idleTimeout) {
            idleKeys.push_back(key);
        }
    }
    for (const std::string& key : idleKeys) {
        TearDownEntry(key, actions);
    }
    return actions;
}

std::vector<BrokerAction> BrokerRouter::Shutdown() {
    std::vector<BrokerAction> actions;
    std::vector<std::string>  keys;
    keys.reserve(languages_.size());
    for (const auto& [key, state] : languages_) {
        keys.push_back(key);
    }
    for (const std::string& key : keys) {
        TearDownEntry(key, actions);
    }
    actions.push_back(BrokerAction{.kind = BrokerAction::Kind::ShutdownProcess});
    return actions;
}

std::size_t BrokerRouter::ConnectionCount() const noexcept {
    return connectionKey_.size();
}

BrokerLanguageStatus BrokerRouter::StatusFor(const std::string& root, const std::string& language) const {
    const auto it = languages_.find(MakeKey(root, language));
    return it == languages_.end() ? BrokerLanguageStatus::NotStarted : it->second.status;
}

} // namespace ned::editor::lsp
