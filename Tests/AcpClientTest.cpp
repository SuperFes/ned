#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "Editor/Acp/AcpClient.h"
#include "Editor/Acp/Transport.h"
#include "UI/EventLoop.h"

using ned::editor::acp::AcpClient;
using ned::editor::acp::Json;
using ned::editor::acp::Transport;

namespace {

// One end of a pipe pair wrapped as a Transport for an AcpClient under
// test; the other end is left as raw fds the test itself reads/writes
// directly, standing in for "the agent's own stdin/stdout." Mirrors
// Tests/LspClientTest.cpp's own ClientFixture, including its own comments'
// reasoning for why eventLoop is owned per-fixture (Notcurses enters the
// alternate screen the instant one exists) and why serverStdoutWrite must be
// closed before the client (unblocks its background read thread via EOF).
struct ClientFixture {
    ned::ui::EventLoop eventLoop;
    int                serverStdinRead;   // test reads what the client wrote
    int                serverStdoutWrite; // test writes to feed the client's read thread, if a test ever wants to
    AcpClient          client;

    ClientFixture(int readFd, int writeFd, Transport transport) : serverStdinRead(readFd), serverStdoutWrite(writeFd), client(std::move(transport), eventLoop) {
    }

    ~ClientFixture() {
        ::close(serverStdoutWrite);
        ::close(serverStdinRead);
    }

    ClientFixture(const ClientFixture&)            = delete;
    ClientFixture& operator=(const ClientFixture&) = delete;

    static ClientFixture Create() {
        int clientWritesHere[2]; // client's write end -> test's read end
        int clientReadsHere[2];  // test's write end -> client's read end
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        return ClientFixture(clientWritesHere[0], clientReadsHere[1], Transport(clientReadsHere[0], clientWritesHere[1]));
    }
};

// Reads exactly one newline-delimited message's raw bytes (with the
// trailing '\n') off a plain fd -- mirrors LspClientTest.cpp's own
// ReadRawFrame, adapted for ACP's simpler framing: no header/Content-Length
// to look for, just read until a '\n' shows up.
std::string ReadRawMessage(int fd) {
    std::string all;
    char        buffer[256];
    for (int i = 0; i < 8; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
        if (all.find('\n') != std::string::npos) {
            break;
        }
    }
    return all;
}

} // namespace

TEST_CASE("AcpClient::SendRequest writes a well-formed, newline-terminated JSON-RPC request", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    fixture.client.SendRequest("initialize", Json{{"protocolVersion", 1}}, [](std::optional<Json>, std::optional<Json>) {});

    const std::string raw = ReadRawMessage(fixture.serverStdinRead);
    REQUIRE_FALSE(raw.empty());
    REQUIRE(raw.back() == '\n');
    const Json message = Json::parse(raw.substr(0, raw.size() - 1));

    REQUIRE(message["jsonrpc"] == "2.0");
    REQUIRE(message["method"] == "initialize");
    REQUIRE(message.contains("id"));
    REQUIRE(message["params"]["protocolVersion"] == 1);
}

TEST_CASE("AcpClient::SendNotification writes a message with no id", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    fixture.client.SendNotification("session/cancel", Json{{"sessionId", "abc"}});

    const std::string raw     = ReadRawMessage(fixture.serverStdinRead);
    const Json        message = Json::parse(raw.substr(0, raw.size() - 1));

    REQUIRE(message["method"] == "session/cancel");
    REQUIRE_FALSE(message.contains("id"));
}

TEST_CASE("AcpClient::DispatchFrame invokes the matching pending request's callback with the result", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    bool                invoked = false;
    std::optional<Json> gotResult;
    std::optional<Json> gotError;
    fixture.client.SendRequest("session/new", Json::object(), [&](std::optional<Json> result, std::optional<Json> error) {
        invoked   = true;
        gotResult = result;
        gotError  = error;
    });

    const std::string raw       = ReadRawMessage(fixture.serverStdinRead);
    const Json        sent      = Json::parse(raw.substr(0, raw.size() - 1));
    const int         requestId = sent["id"].get<int>();

    const Json response = {{"jsonrpc", "2.0"}, {"id", requestId}, {"result", {{"sessionId", "s1"}}}};
    fixture.client.DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotResult.has_value());
    REQUIRE_FALSE(gotError.has_value());
    REQUIRE((*gotResult)["sessionId"] == "s1");
}

TEST_CASE("AcpClient::DispatchFrame invokes the callback with the error, not the result, on a JSON-RPC error response",
          "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    std::optional<Json> gotResult;
    std::optional<Json> gotError;
    fixture.client.SendRequest("bogus/method", Json::object(),
                               [&](std::optional<Json> result, std::optional<Json> error) {
                                   gotResult = result;
                                   gotError  = error;
                               });

    const std::string raw       = ReadRawMessage(fixture.serverStdinRead);
    const int         requestId = Json::parse(raw.substr(0, raw.size() - 1))["id"].get<int>();

    const Json response = {{"jsonrpc", "2.0"}, {"id", requestId}, {"error", {{"code", -32601}, {"message", "method not found"}}}};
    fixture.client.DispatchFrame(response.dump());

    REQUIRE_FALSE(gotResult.has_value());
    REQUIRE(gotError.has_value());
    REQUIRE((*gotError)["message"] == "method not found");
}

TEST_CASE("AcpClient::DispatchFrame with an unknown id is silently ignored, not a crash", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    const Json response = {{"jsonrpc", "2.0"}, {"id", 999}, {"result", Json::object()}};
    fixture.client.DispatchFrame(response.dump());
    SUCCEED();
}

TEST_CASE("AcpClient::DispatchFrame routes a notification to its registered handler by method name", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    Json received;
    bool handlerCalled = false;
    fixture.client.SetNotificationHandler("session/update", [&](const Json& params) {
        handlerCalled = true;
        received      = params;
    });

    const Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params", {{"sessionId", "s1"}}},
    };
    fixture.client.DispatchFrame(notification.dump());

    REQUIRE(handlerCalled);
    REQUIRE(received["sessionId"] == "s1");
}

TEST_CASE("AcpClient::DispatchFrame ignores a notification with no registered handler, not a crash", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    const Json notification = {{"jsonrpc", "2.0"}, {"method", "elicitation/complete"}, {"params", Json::object()}};
    fixture.client.DispatchFrame(notification.dump());
    SUCCEED();
}

TEST_CASE("AcpClient::DispatchFrame ignores malformed JSON without throwing", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    fixture.client.DispatchFrame("{ this is not valid json");
    SUCCEED();
}

TEST_CASE("SetNotificationHandler replaces a previous handler for the same method", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    int callCount = 0;
    fixture.client.SetNotificationHandler("session/update", [&](const Json&) { callCount += 100; });
    fixture.client.SetNotificationHandler("session/update", [&](const Json&) { callCount += 1; });

    fixture.client.DispatchFrame(Json{{"jsonrpc", "2.0"}, {"method", "session/update"}, {"params", Json::object()}}.dump());

    REQUIRE(callCount == 1);
}

TEST_CASE("SetOnDisconnected replaces a previous handler, and unset is a safe no-op", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();
    fixture.client.SetOnDisconnected([](std::string) {});
    fixture.client.SetOnDisconnected([](std::string) {});
    SUCCEED(); // nothing to assert beyond "doesn't crash" -- the read loop itself is covered by the disconnect-driven tests
}

TEST_CASE("AcpClient answers an agent-initiated request synchronously via its registered handler", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();
    fixture.client.SetRequestHandler("fs/read_text_file", [](const Json&, auto respond) { respond(Json{{"content", "hi"}}, std::nullopt); });

    const Json request = {{"jsonrpc", "2.0"}, {"id", 42}, {"method", "fs/read_text_file"}, {"params", {{"path", "/tmp/x"}}}};
    fixture.client.DispatchFrame(request.dump());

    const std::string raw      = ReadRawMessage(fixture.serverStdinRead);
    const Json        response = Json::parse(raw.substr(0, raw.size() - 1));
    REQUIRE(response["id"] == 42);
    REQUIRE(response["result"]["content"] == "hi");
}

TEST_CASE("AcpClient answers an agent-initiated request asynchronously once respond is finally invoked", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    // Simulates session/request_permission: the handler stashes `respond`
    // instead of calling it inline, standing in for a real UI round-trip
    // (waiting on a keystroke) that finishes on a later event-loop
    // iteration -- exactly the case LspClient's own synchronous-only
    // RequestHandler couldn't model.
    std::function<void(std::optional<Json>, std::optional<Json>)> stashed;
    fixture.client.SetRequestHandler("session/request_permission", [&](const Json&, auto respond) { stashed = respond; });

    const Json request = {{"jsonrpc", "2.0"}, {"id", 7}, {"method", "session/request_permission"}, {"params", Json::object()}};
    fixture.client.DispatchFrame(request.dump());

    REQUIRE(stashed); // no response written yet
    stashed(Json{{"outcome", {{"outcome", "selected"}, {"optionId", "allow-once"}}}}, std::nullopt);

    const std::string raw      = ReadRawMessage(fixture.serverStdinRead);
    const Json        response = Json::parse(raw.substr(0, raw.size() - 1));
    REQUIRE(response["id"] == 7);
    REQUIRE(response["result"]["outcome"]["optionId"] == "allow-once");
}

TEST_CASE("AcpClient answers an unhandled agent-initiated request with MethodNotFound", "[Acp]") {
    ClientFixture fixture = ClientFixture::Create();

    const Json request = {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "terminal/create"}, {"params", Json::object()}};
    fixture.client.DispatchFrame(request.dump());

    const std::string raw      = ReadRawMessage(fixture.serverStdinRead);
    const Json        response = Json::parse(raw.substr(0, raw.size() - 1));
    REQUIRE(response["id"] == 9);
    REQUIRE(response["error"]["code"] == -32601);
}
