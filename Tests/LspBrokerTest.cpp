#include <catch2/catch_test_macros.hpp>

#include "Editor/Lsp/LspBroker.h"

using ned::editor::lsp::BrokerAction;
using ned::editor::lsp::BrokerLanguageStatus;
using ned::editor::lsp::BrokerRouter;
using ned::editor::lsp::ConnectionId;
using ned::editor::lsp::Json;

namespace {

    std::size_t CountKind(const std::vector<BrokerAction>& actions, BrokerAction::Kind kind) {
        std::size_t count = 0;
        for (const BrokerAction& action : actions) {
            if (action.kind == kind) {
                ++count;
            }
        }
        return count;
    }

    const BrokerAction* FindKind(const std::vector<BrokerAction>& actions, BrokerAction::Kind kind) {
        for (const BrokerAction& action : actions) {
            if (action.kind == kind) {
                return &action;
            }
        }
        return nullptr;
    }

    Json InitializeFrame(int id) {
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "initialize"}, {"params", Json{{"processId", nullptr}}}};
    }

    Json InitializedFrame() {
        return Json{{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", Json::object()}};
    }

    // Drives (conn, root, language) through a full attach -> real handshake
    // -> Ready -> client "initialized" sequence, using a synthetic id for
    // the client's own initialize request. Shared setup for every test that
    // just needs a Ready entry with one attached client to build on.
    void BringToReady(BrokerRouter& router, ConnectionId conn, const std::string& root, const std::string& language, int clientInitId = 1) {
        (void) router.ClientAttached(conn, root, language, {"clangd"});
        (void) router.ClientFrame(conn, InitializeFrame(clientInitId));
        const auto spawned      = router.ServerSpawned(root, language);
        const int  handshakeId  = FindKind(spawned, BrokerAction::Kind::SendToServer)->frame.at("id").get<int>();
        (void) router.ServerFrame(root, language, Json{{"jsonrpc", "2.0"}, {"id", handshakeId}, {"result", Json::object()}});
        (void) router.ClientFrame(conn, InitializedFrame());
    }

} // namespace

TEST_CASE("BrokerRouter spawns on first attach, ignores argv on later attaches", "[LspBroker]") {
    BrokerRouter router;
    auto         first = router.ClientAttached(1, "/proj", "cpp", {"clangd"});
    REQUIRE(CountKind(first, BrokerAction::Kind::SpawnServer) == 1);
    REQUIRE(FindKind(first, BrokerAction::Kind::SpawnServer)->argv == std::vector<std::string>{"clangd"});
    REQUIRE(FindKind(first, BrokerAction::Kind::SpawnServer)->root == "/proj");
    REQUIRE(router.StatusFor("/proj", "cpp") == BrokerLanguageStatus::SpawningProcess);

    auto second = router.ClientAttached(2, "/proj", "cpp", {"some-other-argv"});
    REQUIRE(CountKind(second, BrokerAction::Kind::SpawnServer) == 0);
}

TEST_CASE("BrokerRouter drives one real handshake and answers every waiting client from cache", "[LspBroker]") {
    BrokerRouter router;
    (void) router.ClientAttached(1, "/proj", "cpp", {"clangd"});
    (void) router.ClientAttached(2, "/proj", "cpp", {});

    // Both clients send their own initialize before the process finishes spawning.
    auto c1Init = router.ClientFrame(1, InitializeFrame(100));
    REQUIRE(c1Init.empty()); // queued, nothing to send yet
    auto c2Init = router.ClientFrame(2, InitializeFrame(200));
    REQUIRE(c2Init.empty());

    // Process comes up -- the real handshake should fire now, using client 1's params.
    auto                 spawned  = router.ServerSpawned("/proj", "cpp");
    const BrokerAction*  realInit = FindKind(spawned, BrokerAction::Kind::SendToServer);
    REQUIRE(realInit != nullptr);
    REQUIRE(realInit->frame.at("method") == "initialize");
    REQUIRE(router.StatusFor("/proj", "cpp") == BrokerLanguageStatus::AwaitingRealHandshake);
    const int brokerHandshakeId = realInit->frame.at("id").get<int>();

    // Real server answers.
    auto serverResponse = router.ServerFrame("/proj", "cpp", Json{{"jsonrpc", "2.0"}, {"id", brokerHandshakeId}, {"result", Json{{"capabilities", Json::object()}}}});
    REQUIRE(router.StatusFor("/proj", "cpp") == BrokerLanguageStatus::Ready);
    // Real "initialized" sent to the server, plus two SendToClient (one per queued client).
    REQUIRE(CountKind(serverResponse, BrokerAction::Kind::SendToClient) == 2);
    const BrokerAction* toServer = FindKind(serverResponse, BrokerAction::Kind::SendToServer);
    REQUIRE(toServer != nullptr);
    REQUIRE(toServer->frame.at("method") == "initialized");

    for (const BrokerAction& action : serverResponse) {
        if (action.kind != BrokerAction::Kind::SendToClient) {
            continue;
        }
        REQUIRE(action.frame.contains("result"));
        if (action.connection == 1) {
            REQUIRE(action.frame.at("id") == 100);
        }
        else {
            REQUIRE(action.connection == 2);
            REQUIRE(action.frame.at("id") == 200);
        }
    }
}

TEST_CASE("BrokerRouter answers a client that attaches after the entry is already Ready", "[LspBroker]") {
    BrokerRouter router;
    BringToReady(router, 1, "/proj", "cpp");

    (void) router.ClientAttached(2, "/proj", "cpp", {});
    auto lateInit = router.ClientFrame(2, InitializeFrame(55));
    REQUIRE(lateInit.size() == 1);
    REQUIRE(lateInit[0].kind == BrokerAction::Kind::SendToClient);
    REQUIRE(lateInit[0].connection == 2);
    REQUIRE(lateInit[0].frame.at("id") == 55);
    REQUIRE(lateInit[0].frame.contains("result"));
}

TEST_CASE("BrokerRouter rewrites request ids and routes the response back to the right client", "[LspBroker]") {
    BrokerRouter router;
    BringToReady(router, 1, "/proj", "cpp");

    auto hoverRequest = router.ClientFrame(1, Json{{"jsonrpc", "2.0"}, {"id", 42}, {"method", "textDocument/hover"}, {"params", Json::object()}});
    REQUIRE(hoverRequest.size() == 1);
    REQUIRE(hoverRequest[0].kind == BrokerAction::Kind::SendToServer);
    const int rewrittenId = hoverRequest[0].frame.at("id").get<int>();
    REQUIRE(rewrittenId != 42); // must not collide with the client's own id space

    auto response = router.ServerFrame("/proj", "cpp", Json{{"jsonrpc", "2.0"}, {"id", rewrittenId}, {"result", Json{{"contents", "hi"}}}});
    REQUIRE(response.size() == 1);
    REQUIRE(response[0].kind == BrokerAction::Kind::SendToClient);
    REQUIRE(response[0].connection == 1);
    REQUIRE(response[0].frame.at("id") == 42); // restored to the client's own original id
}

TEST_CASE("BrokerRouter broadcasts a server notification to every attached client", "[LspBroker]") {
    BrokerRouter router;
    (void) router.ClientAttached(1, "/proj", "cpp", {"clangd"});
    (void) router.ClientAttached(2, "/proj", "cpp", {});
    (void) router.ClientFrame(1, InitializeFrame(1));
    (void) router.ClientFrame(2, InitializeFrame(2));
    int handshakeId = FindKind(router.ServerSpawned("/proj", "cpp"), BrokerAction::Kind::SendToServer)->frame.at("id").get<int>();
    (void) router.ServerFrame("/proj", "cpp", Json{{"jsonrpc", "2.0"}, {"id", handshakeId}, {"result", Json::object()}});
    (void) router.ClientFrame(1, InitializedFrame());
    (void) router.ClientFrame(2, InitializedFrame());

    auto diagnostics = router.ServerFrame(
        "/proj", "cpp", Json{{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", Json{{"uri", "file:///a.cpp"}}}});
    REQUIRE(diagnostics.size() == 2);
    REQUIRE(CountKind(diagnostics, BrokerAction::Kind::SendToClient) == 2);
}

TEST_CASE("BrokerRouter auto-acknowledges a server-initiated request without routing it to any client", "[LspBroker]") {
    BrokerRouter router;
    BringToReady(router, 1, "/proj", "cpp");

    auto progressCreate = router.ServerFrame(
        "/proj", "cpp", Json{{"jsonrpc", "2.0"}, {"id", 999}, {"method", "window/workDoneProgress/create"}, {"params", Json{{"token", "t"}}}});
    REQUIRE(progressCreate.size() == 1);
    REQUIRE(progressCreate[0].kind == BrokerAction::Kind::SendToServer);
    REQUIRE(progressCreate[0].frame.at("id") == 999);
    REQUIRE(progressCreate[0].frame.at("result").is_null());
}

TEST_CASE("BrokerRouter flushes queued clients with an error when the real spawn fails", "[LspBroker]") {
    BrokerRouter router;
    (void) router.ClientAttached(1, "/proj", "cpp", {"clangd"});
    (void) router.ClientAttached(2, "/proj", "cpp", {});
    (void) router.ClientFrame(1, InitializeFrame(11));
    (void) router.ClientFrame(2, InitializeFrame(22));

    auto failed = router.ServerSpawnFailed("/proj", "cpp", "clangd: no such file or directory");
    REQUIRE(router.StatusFor("/proj", "cpp") == BrokerLanguageStatus::Failed);
    REQUIRE(CountKind(failed, BrokerAction::Kind::SendToClient) == 2);
    for (const BrokerAction& action : failed) {
        REQUIRE(action.frame.contains("error"));
    }

    // A client attaching after Failed gets an immediate error too, no respawn attempt.
    (void) router.ClientAttached(3, "/proj", "cpp", {"whatever"});
    auto lateInit = router.ClientFrame(3, InitializeFrame(33));
    REQUIRE(lateInit.size() == 1);
    REQUIRE(lateInit[0].frame.contains("error"));
}

TEST_CASE("BrokerRouter flushes queued clients with an error when the real handshake itself errors", "[LspBroker]") {
    BrokerRouter router;
    (void) router.ClientAttached(1, "/proj", "cpp", {"clangd"});
    (void) router.ClientFrame(1, InitializeFrame(1));
    int handshakeId = FindKind(router.ServerSpawned("/proj", "cpp"), BrokerAction::Kind::SendToServer)->frame.at("id").get<int>();

    auto errored = router.ServerFrame("/proj", "cpp", Json{{"jsonrpc", "2.0"}, {"id", handshakeId}, {"error", Json{{"code", -1}, {"message", "boom"}}}});
    REQUIRE(router.StatusFor("/proj", "cpp") == BrokerLanguageStatus::Failed);
    REQUIRE(CountKind(errored, BrokerAction::Kind::SendToClient) == 1);
    REQUIRE(errored[0].frame.at("error").at("message") == "boom");
}

TEST_CASE("BrokerRouter disconnecting mid-handshake doesn't affect a later client sharing the same handshake", "[LspBroker]") {
    BrokerRouter router;
    (void) router.ClientAttached(1, "/proj", "cpp", {"clangd"});
    (void) router.ClientAttached(2, "/proj", "cpp", {});
    (void) router.ClientFrame(1, InitializeFrame(1));
    (void) router.ClientFrame(2, InitializeFrame(2));
    int handshakeId = FindKind(router.ServerSpawned("/proj", "cpp"), BrokerAction::Kind::SendToServer)->frame.at("id").get<int>();

    // Client 1 (whose params drove the real handshake) disconnects before the server answers.
    auto disconnectActions = router.ClientDisconnected(1);
    REQUIRE(disconnectActions.empty());

    auto response = router.ServerFrame("/proj", "cpp", Json{{"jsonrpc", "2.0"}, {"id", handshakeId}, {"result", Json::object()}});
    REQUIRE(router.StatusFor("/proj", "cpp") == BrokerLanguageStatus::Ready);
    // Only client 2 is still queued -- client 1's entry was dropped by ClientDisconnected.
    REQUIRE(CountKind(response, BrokerAction::Kind::SendToClient) == 1);
    REQUIRE(FindKind(response, BrokerAction::Kind::SendToClient)->connection == 2);
}

TEST_CASE("BrokerRouter closes every attached client and resets state when the real server disconnects", "[LspBroker]") {
    BrokerRouter router;
    (void) router.ClientAttached(1, "/proj", "cpp", {"clangd"});
    (void) router.ClientAttached(2, "/proj", "cpp", {});
    (void) router.ClientFrame(1, InitializeFrame(1));
    (void) router.ClientFrame(2, InitializeFrame(2));
    int handshakeId = FindKind(router.ServerSpawned("/proj", "cpp"), BrokerAction::Kind::SendToServer)->frame.at("id").get<int>();
    (void) router.ServerFrame("/proj", "cpp", Json{{"jsonrpc", "2.0"}, {"id", handshakeId}, {"result", Json::object()}});
    (void) router.ClientFrame(1, InitializedFrame());
    (void) router.ClientFrame(2, InitializedFrame());
    REQUIRE(router.ConnectionCount() == 2);

    auto crashed = router.ServerDisconnected("/proj", "cpp");
    REQUIRE(CountKind(crashed, BrokerAction::Kind::CloseClient) == 2);
    REQUIRE(CountKind(crashed, BrokerAction::Kind::CloseServer) == 1);
    REQUIRE(router.StatusFor("/proj", "cpp") == BrokerLanguageStatus::NotStarted);
    REQUIRE(router.ConnectionCount() == 0);

    // A fresh attach after a crash spawns again from scratch.
    auto respawn = router.ClientAttached(3, "/proj", "cpp", {"clangd"});
    REQUIRE(CountKind(respawn, BrokerAction::Kind::SpawnServer) == 1);
}

TEST_CASE("BrokerRouter's Shutdown sends a real LSP shutdown/exit and closes every client, across every project", "[LspBroker]") {
    BrokerRouter router;
    BringToReady(router, 1, "/proj-a", "cpp");
    BringToReady(router, 2, "/proj-b", "python");

    auto shutdown = router.Shutdown();
    REQUIRE(CountKind(shutdown, BrokerAction::Kind::CloseClient) == 2);
    REQUIRE(CountKind(shutdown, BrokerAction::Kind::ShutdownProcess) == 1);
    REQUIRE(CountKind(shutdown, BrokerAction::Kind::SendToServer) == 4); // "shutdown" + "exit" per project
    REQUIRE(CountKind(shutdown, BrokerAction::Kind::CloseServer) == 2);
    REQUIRE(router.ConnectionCount() == 0);
    REQUIRE(router.StatusFor("/proj-a", "cpp") == BrokerLanguageStatus::NotStarted);
    REQUIRE(router.StatusFor("/proj-b", "python") == BrokerLanguageStatus::NotStarted);
}

TEST_CASE("BrokerRouter ignores a frame from a connection that never attached", "[LspBroker]") {
    BrokerRouter router;
    auto         actions = router.ClientFrame(42, InitializeFrame(1));
    REQUIRE(actions.empty());
}

TEST_CASE("BrokerRouter keeps two different projects' same-language entries fully independent", "[LspBroker]") {
    BrokerRouter router;
    BringToReady(router, 1, "/proj-a", "cpp", 100);
    BringToReady(router, 2, "/proj-b", "cpp", 200);

    REQUIRE(router.StatusFor("/proj-a", "cpp") == BrokerLanguageStatus::Ready);
    REQUIRE(router.StatusFor("/proj-b", "cpp") == BrokerLanguageStatus::Ready);

    // A notification from proj-a's real server must never reach proj-b's client.
    auto diagnostics = router.ServerFrame(
        "/proj-a", "cpp", Json{{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", Json{{"uri", "file:///a.cpp"}}}});
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics[0].connection == 1);
}

TEST_CASE("BrokerRouter's LRU eviction picks the oldest idle entry under pressure, never a busy one", "[LspBroker]") {
    using Clock = std::chrono::steady_clock;
    BrokerRouter router(/*maxConcurrentServers=*/2);
    const auto   t0 = Clock::now();

    // proj-a becomes Ready and idle (client disconnects, no one attached) at t0.
    BringToReady(router, 1, "/proj-a", "cpp", 1);
    (void) router.ClientDisconnected(1);
    REQUIRE(router.StatusFor("/proj-a", "cpp") == BrokerLanguageStatus::Ready);

    // proj-b becomes Ready and STAYS attached (busy) at t0 + 1s -- more recently active than proj-a.
    (void) router.ClientAttached(2, "/proj-b", "python", {"pylsp"}, t0 + std::chrono::seconds(1));
    (void) router.ClientFrame(2, InitializeFrame(2), t0 + std::chrono::seconds(1));
    int handshakeId = FindKind(router.ServerSpawned("/proj-b", "python"), BrokerAction::Kind::SendToServer)->frame.at("id").get<int>();
    (void) router.ServerFrame("/proj-b", "python", Json{{"jsonrpc", "2.0"}, {"id", handshakeId}, {"result", Json::object()}}, t0 + std::chrono::seconds(1));
    (void) router.ClientFrame(2, InitializedFrame(), t0 + std::chrono::seconds(1));

    // Now at capacity (2). A third project attaches -- proj-a (idle, oldest) should be evicted, not proj-b (busy).
    auto third = router.ClientAttached(3, "/proj-c", "rust", {"rust-analyzer"}, t0 + std::chrono::seconds(2));
    REQUIRE(CountKind(third, BrokerAction::Kind::SpawnServer) == 1); // proj-c spawns
    REQUIRE(router.StatusFor("/proj-a", "cpp") == BrokerLanguageStatus::NotStarted); // evicted
    REQUIRE(router.StatusFor("/proj-b", "python") == BrokerLanguageStatus::Ready);   // untouched -- was busy
    REQUIRE(router.StatusFor("/proj-c", "rust") == BrokerLanguageStatus::SpawningProcess);
}

TEST_CASE("BrokerRouter exceeds the cap rather than disrupting anything when every entry is busy", "[LspBroker]") {
    BrokerRouter router(/*maxConcurrentServers=*/1);
    BringToReady(router, 1, "/proj-a", "cpp"); // stays attached -- busy
    REQUIRE(router.ConnectionCount() == 1);

    auto third = router.ClientAttached(2, "/proj-b", "python", {"pylsp"});
    REQUIRE(CountKind(third, BrokerAction::Kind::SpawnServer) == 1); // spawns anyway, cap exceeded
    REQUIRE(router.StatusFor("/proj-a", "cpp") == BrokerLanguageStatus::Ready); // untouched
}

TEST_CASE("BrokerRouter's IdleSweep tears down only entries idle past the timeout with no live clients", "[LspBroker]") {
    using Clock = std::chrono::steady_clock;
    BrokerRouter router;
    const auto   t0 = Clock::now();

    BringToReady(router, 1, "/proj-a", "cpp", 1); // will go idle
    (void) router.ClientDisconnected(1);
    BringToReady(router, 2, "/proj-b", "python", 2); // stays attached -- never idle-eligible

    auto tooSoon = router.IdleSweep(t0, std::chrono::minutes(30));
    REQUIRE(tooSoon.empty());

    auto swept = router.IdleSweep(t0 + std::chrono::minutes(31), std::chrono::minutes(30));
    REQUIRE(router.StatusFor("/proj-a", "cpp") == BrokerLanguageStatus::NotStarted);
    REQUIRE(router.StatusFor("/proj-b", "python") == BrokerLanguageStatus::Ready);
    REQUIRE(CountKind(swept, BrokerAction::Kind::CloseClient) == 0); // proj-a had no attached clients left to close
}
