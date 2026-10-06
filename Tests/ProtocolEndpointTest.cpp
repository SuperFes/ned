#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/Endpoint.h"

using ned::editor::protocol::DecodeCbor;
using ned::editor::protocol::EncodeCbor;
using ned::editor::protocol::Endpoint;
using ned::editor::protocol::kDefaultWindow;
using ned::editor::protocol::ProtocolError;
using ned::editor::protocol::RemoteError;
using ned::editor::protocol::Responder;
using ned::editor::protocol::ResponseHandlers;
namespace error_code = ned::editor::protocol::error_code;
using Json           = nlohmann::json;

namespace {

// Two endpoints whose messages wait in queues until pumped, each crossing
// the CBOR codec the way it would cross a real wire.
struct Peers {
    std::deque<Json> toHost;
    std::deque<Json> toEditor;
    Endpoint         editor{[this](Json message) { toHost.push_back(std::move(message)); }};
    Endpoint         host{[this](Json message) { toEditor.push_back(std::move(message)); }};

    void DeliverToHost() {
        Json message = std::move(toHost.front());
        toHost.pop_front();
        host.Receive(DecodeCbor(EncodeCbor(message)));
    }

    void DeliverToEditor() {
        Json message = std::move(toEditor.front());
        toEditor.pop_front();
        editor.Receive(DecodeCbor(EncodeCbor(message)));
    }

    void Pump() {
        while (!toHost.empty() || !toEditor.empty()) {
            if (!toHost.empty()) {
                DeliverToHost();
            }
            if (!toEditor.empty()) {
                DeliverToEditor();
            }
        }
    }
};

struct Outcome {
    std::optional<Json>        result;
    std::optional<RemoteError> error;
    std::vector<Json>          chunks;
    int                        responses = 0;

    ResponseHandlers Handlers() {
        return ResponseHandlers{
            [this](Json value) {
                result = std::move(value);
                ++responses;
            },
            [this](RemoteError value) {
                error = std::move(value);
                ++responses;
            },
            [this](Json data) { chunks.push_back(std::move(data)); }};
    }
};

std::size_t CountChunks(const std::deque<Json>& queue) {
    std::size_t count = 0;
    for (const Json& message : queue) {
        count += message.value("method", "") == "$/chunk" ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("Endpoint correlates responses answered out of order", "[Protocol][Endpoint]") {
    Peers                  peers;
    std::vector<Responder> held;
    peers.host.Handle("echo", [&held](Json params, Responder responder) {
        responder.Chunk(params); // an ordinary request may still stream
        held.push_back(std::move(responder));
    });

    Outcome first;
    Outcome second;
    peers.editor.Request("echo", "one", first.Handlers());
    peers.editor.Request("echo", "two", second.Handlers());
    peers.Pump();
    REQUIRE(held.size() == 2);
    REQUIRE(peers.editor.PendingRequests() == 2);

    held[1].Result("second");
    held[0].Result("first");
    peers.Pump();

    REQUIRE(first.result == Json("first"));
    REQUIRE(first.chunks == std::vector<Json>{"one"});
    REQUIRE(second.result == Json("second"));
    REQUIRE(second.chunks == std::vector<Json>{"two"});
    REQUIRE(peers.editor.PendingRequests() == 0);
}

TEST_CASE("Endpoint numbers requests past ids a handshake already used", "[Protocol][Endpoint]") {
    Peers peers;
    peers.host.Handle("echo", [](Json params, Responder responder) { responder.Result(std::move(params)); });

    // The handshake's request, sent outside the editor's endpoint.
    peers.host.Receive(Json{{"id", 1}, {"method", "echo"}, {"params", "hello"}});
    peers.editor.ReserveRequestIds(1);
    peers.toEditor.clear();

    Outcome outcome;
    REQUIRE(peers.editor.Request("echo", "after", outcome.Handlers()) == 2);
    peers.Pump();
    REQUIRE(outcome.result == Json("after"));

    // Never backwards.
    peers.editor.ReserveRequestIds(0);
    REQUIRE(peers.editor.Request("echo", "next", outcome.Handlers()) == 3);
}

TEST_CASE("Endpoint answers an unknown method and dispatches notifications", "[Protocol][Endpoint]") {
    Peers             peers;
    std::vector<Json> seen;
    peers.host.HandleNotification("note", [&seen](Json params) { seen.push_back(std::move(params)); });

    Outcome outcome;
    peers.editor.Request("nope", nullptr, outcome.Handlers());
    peers.editor.Notify("note", {{"k", 1}});
    peers.editor.Notify("unhandled", nullptr);
    peers.Pump();

    REQUIRE(outcome.error.has_value());
    REQUIRE(outcome.error->code == error_code::kMethodNotFound);
    REQUIRE(seen == std::vector<Json>{Json{{"k", 1}}});
}

TEST_CASE("Endpoint gives every request exactly one response", "[Protocol][Endpoint]") {
    Peers peers;
    peers.host.Handle("drop", [](Json, Responder) {});
    peers.host.Handle("throw", [](Json, Responder) { throw std::runtime_error("boom"); });
    peers.host.Handle("twice", [](Json, Responder responder) {
        responder.Result(1);
        responder.Result(2);
        responder.Error(error_code::kInternalError, "late");
    });

    Outcome dropped;
    Outcome thrown;
    Outcome twice;
    peers.editor.Request("drop", nullptr, dropped.Handlers());
    peers.editor.Request("throw", nullptr, thrown.Handlers());
    peers.editor.Request("twice", nullptr, twice.Handlers());
    peers.Pump();

    REQUIRE(dropped.responses == 1);
    REQUIRE(dropped.error->code == error_code::kInternalError);
    REQUIRE(thrown.responses == 1);
    REQUIRE(thrown.error->code == error_code::kInternalError);
    REQUIRE(twice.responses == 1);
    REQUIRE(twice.result == Json(1));
}

TEST_CASE("Endpoint streams a flood without exceeding the receiver's window", "[Protocol][Endpoint]") {
    Peers         peers;
    constexpr int kChunks = 1000;
    peers.host.Handle("flood", [](Json, Responder responder) {
        for (int i = 0; i < kChunks; ++i) {
            responder.Chunk(i);
        }
        REQUIRE(responder.Backlog() == kChunks - kDefaultWindow);
        responder.Result("done");
    });

    Outcome outcome;
    peers.editor.Request("flood", nullptr, outcome.Handlers());
    while (!peers.toHost.empty() || !peers.toEditor.empty()) {
        REQUIRE(CountChunks(peers.toEditor) <= kDefaultWindow);
        if (!peers.toHost.empty()) {
            peers.DeliverToHost();
        }
        else {
            peers.DeliverToEditor();
        }
    }

    REQUIRE(outcome.result == Json("done"));
    REQUIRE(outcome.chunks.size() == kChunks);
    for (int i = 0; i < kChunks; ++i) {
        REQUIRE(outcome.chunks[static_cast<std::size_t>(i)] == Json(i));
    }
}

TEST_CASE("Endpoint tells a producer when credit clears its backlog", "[Protocol][Endpoint]") {
    Peers                    peers;
    std::optional<Responder> held;
    int                      drained = 0;
    peers.host.Handle("stream", [&](Json, Responder responder) {
        for (std::size_t i = 0; i < kDefaultWindow + 2; ++i) {
            responder.Chunk(i);
        }
        REQUIRE(responder.Backlog() == 2);
        responder.OnDrained([&] {
            ++drained;
            REQUIRE(held->Backlog() == 0);
            held->Chunk("late");
            held->Result("done");
        });
        held.emplace(std::move(responder));
    });

    Outcome outcome;
    peers.editor.Request("stream", nullptr, outcome.Handlers());
    peers.Pump();

    REQUIRE(drained == 1);
    REQUIRE(outcome.result == Json("done"));
    REQUIRE(outcome.chunks.size() == kDefaultWindow + 3);
    REQUIRE(outcome.chunks.back() == Json("late"));
}

TEST_CASE("Endpoint cancels mid-stream with one response and no later chunks", "[Protocol][Endpoint]") {
    Peers                    peers;
    std::optional<Responder> held;
    peers.host.Handle("stream", [&held](Json, Responder responder) {
        for (int i = 0; i < 100; ++i) {
            responder.Chunk(i);
        }
        held.emplace(std::move(responder));
    });

    Outcome             outcome;
    const std::uint64_t id = peers.editor.Request("stream", nullptr, outcome.Handlers());
    peers.DeliverToHost();
    peers.DeliverToEditor();
    peers.DeliverToEditor();
    REQUIRE(outcome.chunks.size() == 2);

    peers.editor.Cancel(id);
    peers.Pump();
    REQUIRE(held->StopToken().stop_requested());
    REQUIRE(held->Finished());

    // The handler noticing late sends nothing more.
    held->Chunk(1000);
    held->Result("partial");
    REQUIRE(peers.toEditor.empty());

    REQUIRE(outcome.responses == 1);
    REQUIRE(outcome.error->code == error_code::kCancelled);
    REQUIRE(outcome.chunks.size() == 2);
    REQUIRE(peers.editor.PendingRequests() == 0);
}

TEST_CASE("Endpoint Close fails pending requests and stops incoming work", "[Protocol][Endpoint]") {
    Peers                    peers;
    std::optional<Responder> held;
    peers.host.Handle("wait", [&held](Json, Responder responder) { held.emplace(std::move(responder)); });

    Outcome outcome;
    peers.editor.Request("wait", nullptr, outcome.Handlers());
    peers.Pump();

    peers.editor.Close("link dropped");
    REQUIRE(outcome.error->code == error_code::kConnectionClosed);
    REQUIRE(outcome.error->message == "link dropped");

    peers.host.Close("link dropped");
    REQUIRE(held->StopToken().stop_requested());
    held->Result("late");
    REQUIRE(peers.toEditor.empty());

    Outcome afterClose;
    peers.editor.Request("wait", nullptr, afterClose.Handlers());
    REQUIRE(afterClose.error->code == error_code::kConnectionClosed);
    REQUIRE(peers.toHost.empty());
}

TEST_CASE("A Responder outliving its Endpoint does nothing", "[Protocol][Endpoint]") {
    std::optional<Responder> held;
    {
        std::vector<Json> sent;
        Endpoint          host([&sent](Json message) { sent.push_back(std::move(message)); });
        host.Handle("wait", [&held](Json, Responder responder) { held.emplace(std::move(responder)); });
        host.Receive(Json{{"id", 1}, {"method", "wait"}, {"params", nullptr}});
        REQUIRE(sent.empty());
    }
    REQUIRE(held->StopToken().stop_requested());
    held->Chunk(1);
    held->Result(2);
}

TEST_CASE("Endpoint rejects messages that break the protocol", "[Protocol][Endpoint]") {
    std::vector<Json>   sent;
    Endpoint            editor([&sent](Json message) { sent.push_back(std::move(message)); });
    Outcome             outcome;
    const std::uint64_t id = editor.Request("stream", nullptr, outcome.Handlers());

    REQUIRE_THROWS_AS(editor.Receive(Json::array()), ProtocolError);
    REQUIRE_THROWS_AS(editor.Receive(Json{{"method", 3}}), ProtocolError);
    REQUIRE_THROWS_AS(editor.Receive(Json{{"id", -1}, {"result", 0}}), ProtocolError);
    REQUIRE_THROWS_AS(editor.Receive(Json{{"id", id + 1}, {"result", 0}}), ProtocolError);
    REQUIRE_THROWS_AS(editor.Receive(Json{{"params", 0}}), ProtocolError);
    REQUIRE_THROWS_AS(editor.Receive(Json{{"method", "$/chunk"}, {"params", {{"id", id}, {"seq", 1}}}}), ProtocolError);

    // Consuming half the window grants it back.
    for (std::uint64_t seq = 0; seq < kDefaultWindow / 2; ++seq) {
        editor.Receive(Json{{"method", "$/chunk"}, {"params", {{"id", id}, {"seq", seq}, {"data", seq}}}});
    }
    REQUIRE(outcome.chunks.size() == kDefaultWindow / 2);
    REQUIRE(sent.back() == Json{{"method", "$/credit"}, {"params", {{"id", id}, {"n", kDefaultWindow / 2}}}});
    REQUIRE_THROWS_AS(editor.Receive(Json{{"method", "$/chunk"}, {"params", {{"id", id}, {"seq", 0}}}}), ProtocolError);

    std::vector<Json> hostSent;
    Endpoint          host([&hostSent](Json message) { hostSent.push_back(std::move(message)); });
    host.Handle("x", [](Json, Responder responder) { responder.Result(nullptr); });
    host.Receive(Json{{"id", 5}, {"method", "x"}});
    REQUIRE_THROWS_AS(host.Receive(Json{{"id", 5}, {"method", "x"}}), ProtocolError); // reused
    REQUIRE_THROWS_AS(host.Receive(Json{{"id", 4}, {"method", "x"}}), ProtocolError); // went backwards
}

TEST_CASE("Endpoint expires a request that hears nothing within its idle limit", "[Protocol][Endpoint]") {
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;
    std::vector<Responder>   stalled;
    std::optional<Responder> streaming;
    Peers                    peers;
    peers.host.Handle("stall", [&](const Json&, Responder responder) { stalled.push_back(std::move(responder)); });
    peers.host.Handle("stream", [&](const Json&, Responder responder) { streaming.emplace(std::move(responder)); });

    Outcome   limited;
    Outcome   unlimited;
    Outcome   chatty;
    const std::uint64_t id = peers.editor.Request("stall", nullptr, limited.Handlers(), milliseconds(1000));
    peers.editor.Request("stall", nullptr, unlimited.Handlers());
    peers.editor.Request("stream", nullptr, chatty.Handlers(), milliseconds(1000));
    peers.Pump();
    const auto sent = steady_clock::now();

    peers.editor.ExpireIdle(sent + milliseconds(500));
    CHECK(limited.responses == 0);

    // A chunk counts as hearing from the request.
    std::this_thread::sleep_for(milliseconds(2));
    streaming->Chunk("progress");
    peers.Pump();
    peers.editor.ExpireIdle(sent + milliseconds(1001));
    CHECK(chatty.responses == 0);
    CHECK(limited.responses == 1);
    CHECK(limited.error->code == error_code::kTimedOut);
    CHECK(limited.error->message == "no answer within 1000 ms");

    // The host is told to stop, and its late answer goes nowhere.
    REQUIRE(peers.toHost.size() == 1);
    CHECK(peers.toHost.front()["method"] == "$/cancel");
    CHECK(peers.toHost.front()["params"]["id"] == id);
    peers.Pump();
    stalled.front().Result("late");
    peers.Pump();
    CHECK(limited.responses == 1);
    CHECK_FALSE(limited.result.has_value());

    peers.editor.ExpireIdle(steady_clock::now() + std::chrono::hours(24));
    CHECK(unlimited.responses == 0);
    CHECK(chatty.responses == 1);
    CHECK(chatty.error->code == error_code::kTimedOut);
    // Expiring again does nothing more.
    peers.editor.ExpireIdle(steady_clock::now() + std::chrono::hours(48));
    CHECK(chatty.responses == 1);
}

TEST_CASE("A request expiring closes the endpoint without losing the others' answers", "[Protocol][Endpoint]") {
    std::vector<Responder> kept;
    Peers                  peers;
    peers.host.Handle("stall", [&](const Json&, Responder responder) { kept.push_back(std::move(responder)); });
    Outcome second;
    peers.editor.Request("stall", nullptr, ResponseHandlers{{}, [&](RemoteError) { peers.editor.Close("gave up"); }, {}}, std::chrono::milliseconds(1));
    peers.editor.Request("stall", nullptr, second.Handlers(), std::chrono::milliseconds(1));
    peers.Pump();
    peers.editor.ExpireIdle(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    CHECK(second.responses == 1);
    CHECK(second.error->code == error_code::kTimedOut);
}
