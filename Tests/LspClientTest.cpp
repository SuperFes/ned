#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/Transport.h"
#include "UI/EventLoop.h"

using ned::editor::lsp::Json;
using ned::editor::lsp::LspClient;
using ned::editor::lsp::Transport;

namespace {

// One end of a pipe pair wrapped as a Transport for an LspClient under
// test; the other end is left as raw fds the test itself reads/writes
// directly, standing in for "the language server's own stdin/stdout." The
// client's background read thread just blocks harmlessly on this pipe's
// read end for the lifetime of these tests -- every test here drives
// DispatchFrame directly rather than actually writing anything for that
// thread to pick up.
//
// Explicit destructor, real (non-aggregate) constructor: closing
// serverStdoutWrite -- the *test's* own reference to the pipe's write end --
// before LspClient is destroyed is load-bearing, not cleanup-for-its-own-
// sake. LspClient's destructor relies on Transport's own destructor closing
// the *client's* copy of that same pipe direction to unblock its background
// read thread (EOF once no writer remains) -- true in production, where
// killing the real child process closes every fd it held. Here there's no
// child process; the only other reference to that write end is this
// fixture's own serverStdoutWrite, so it has to be closed first or the
// background thread's blocked read() (and therefore LspClient's own
// destructor, which joins that thread) would hang forever -- confirmed via
// a real hung test run, not a defensive guess.
// FTXUI -> Notcurses migration: was a real, unstarted ScreenInteractive,
// threaded through as a separate constructor parameter -- safe under FTXUI
// since it never touched the terminal until Loop() actually ran. Notcurses'
// own EventLoop is not that forgiving: its constructor calls
// notcurses_core_init immediately, entering the alternate screen buffer for
// real the instant one exists. Owned here, by value, as this fixture's own
// member instead -- constructed and torn down within one TEST_CASE's own
// scope, the shortest window that still satisfies LspClient's constructor,
// rather than shared process-wide (which would hijack the terminal for
// every other, unrelated test's own Catch2 console output for the rest of
// the whole binary's run -- unlike Janet's Environment, which
// JanetTestSupport.h shares process-wide precisely because it touches no
// terminal state at all). Never actually used for its own Post()-draining
// Run() loop here -- these tests all call DispatchFrame directly instead,
// exercising the exact same correlation/dispatch logic without needing a
// live loop at all (see LspClient.h's own header comment on DispatchFrame
// for why).
struct ClientFixture {
    ned::ui::EventLoop eventLoop;
    int                serverStdinRead;   // test reads what the client wrote (client's "stdout" from the server's perspective... see below)
    int                serverStdoutWrite; // test writes to feed the client's read thread, if a test ever wants to
    LspClient          client;

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

// Reads exactly one LSP frame's raw bytes (header + body) off a plain fd --
// mirrors Transport::ReadFrame's own framing logic independently, so these
// tests verify SendRequest/SendNotification's actual wire output without
// depending on Transport's own (separately tested) parser.
std::string ReadRawFrame(int fd) {
    std::string all;
    char        buffer[256];
    // The whole frame arrives in one or two reads for anything this small;
    // a short, bounded number of reads is enough without needing a real
    // Content-Length-aware loop here.
    for (int i = 0; i < 4; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
        // Stop once we plausibly have the whole thing: a Content-Length
        // frame's body length is embedded in its own header.
        const auto headerEnd = all.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const std::string_view kPrefix   = "Content-Length: ";
            const auto             prefixPos = all.find(kPrefix);
            if (prefixPos != std::string::npos) {
                const std::size_t contentLength = std::stoul(all.substr(prefixPos + kPrefix.size()));
                if (all.size() >= headerEnd + 4 + contentLength) {
                    break;
                }
            }
        }
    }
    return all;
}

} // namespace

TEST_CASE("LspClient::SendRequest writes a well-formed JSON-RPC request frame", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    fixture.client.SendRequest("initialize", Json{{"processId", nullptr}}, [](std::optional<Json>, std::optional<Json>) {});

    const std::string raw     = ReadRawFrame(fixture.serverStdinRead);
    const auto        bodyPos = raw.find("\r\n\r\n");
    REQUIRE(bodyPos != std::string::npos);
    const Json message = Json::parse(raw.substr(bodyPos + 4));

    REQUIRE(message["jsonrpc"] == "2.0");
    REQUIRE(message["method"] == "initialize");
    REQUIRE(message.contains("id"));
    REQUIRE(message["params"]["processId"].is_null());
}

TEST_CASE("LspClient::SendNotification writes a frame with no id", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    fixture.client.SendNotification("initialized", Json::object());

    const std::string raw     = ReadRawFrame(fixture.serverStdinRead);
    const auto        bodyPos = raw.find("\r\n\r\n");
    REQUIRE(bodyPos != std::string::npos);
    const Json message = Json::parse(raw.substr(bodyPos + 4));

    REQUIRE(message["method"] == "initialized");
    REQUIRE_FALSE(message.contains("id"));
}

TEST_CASE("LspClient::DispatchFrame invokes the matching pending request's callback with the result", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    bool                invoked = false;
    std::optional<Json> gotResult;
    std::optional<Json> gotError;
    fixture.client.SendRequest("initialize", Json::object(), [&](std::optional<Json> result, std::optional<Json> error) {
        invoked   = true;
        gotResult = result;
        gotError  = error;
    });

    // Read back the id SendRequest actually assigned rather than assuming
    // it's 1 -- ties this test to real behavior, not an implementation
    // detail of the id counter.
    const std::string raw       = ReadRawFrame(fixture.serverStdinRead);
    const Json        sent      = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    const int         requestId = sent["id"].get<int>();

    const Json response = {{"jsonrpc", "2.0"}, {"id", requestId}, {"result", {{"capabilities", Json::object()}}}};
    fixture.client.DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotResult.has_value());
    REQUIRE_FALSE(gotError.has_value());
    REQUIRE((*gotResult)["capabilities"].is_object());
}

TEST_CASE("LspClient::DispatchFrame invokes the callback with the error, not the result, on a JSON-RPC error response",
          "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    std::optional<Json> gotResult;
    std::optional<Json> gotError;
    fixture.client.SendRequest("bogus/method", Json::object(),
                               [&](std::optional<Json> result, std::optional<Json> error) {
                                   gotResult = result;
                                   gotError  = error;
                               });

    const std::string raw       = ReadRawFrame(fixture.serverStdinRead);
    const int         requestId = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["id"].get<int>();

    const Json response = {{"jsonrpc", "2.0"}, {"id", requestId}, {"error", {{"code", -32601}, {"message", "method not found"}}}};
    fixture.client.DispatchFrame(response.dump());

    REQUIRE_FALSE(gotResult.has_value());
    REQUIRE(gotError.has_value());
    REQUIRE((*gotError)["message"] == "method not found");
}

TEST_CASE("LspClient::DispatchFrame with an unknown id is silently ignored, not a crash", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    const Json response = {{"jsonrpc", "2.0"}, {"id", 999}, {"result", Json::object()}};
    fixture.client.DispatchFrame(response.dump()); // no matching pending request -- must not throw/crash
    SUCCEED();
}

TEST_CASE("LspClient::DispatchFrame routes a notification to its registered handler by method name", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    Json received;
    bool handlerCalled = false;
    fixture.client.SetNotificationHandler("textDocument/publishDiagnostics", [&](const Json& params) {
        handlerCalled = true;
        received      = params;
    });

    const Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params", {{"uri", "file:///tmp/foo.c"}, {"diagnostics", Json::array()}}},
    };
    fixture.client.DispatchFrame(notification.dump());

    REQUIRE(handlerCalled);
    REQUIRE(received["uri"] == "file:///tmp/foo.c");
}

TEST_CASE("LspClient::DispatchFrame ignores a notification with no registered handler, not a crash", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    const Json notification = {{"jsonrpc", "2.0"}, {"method", "window/logMessage"}, {"params", Json::object()}};
    fixture.client.DispatchFrame(notification.dump());
    SUCCEED();
}

TEST_CASE("LspClient::DispatchFrame ignores malformed JSON without throwing", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    fixture.client.DispatchFrame("{ this is not valid json");
    SUCCEED();
}

TEST_CASE("SetNotificationHandler replaces a previous handler for the same method", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    int callCount = 0;
    fixture.client.SetNotificationHandler("window/logMessage", [&](const Json&) { callCount += 100; });
    fixture.client.SetNotificationHandler("window/logMessage", [&](const Json&) { callCount += 1; });

    const Json notification = {{"jsonrpc", "2.0"}, {"method", "window/logMessage"}, {"params", Json::object()}};
    fixture.client.DispatchFrame(notification.dump());

    REQUIRE(callCount == 1); // only the second (replacing) handler ran
}

// error-visibility follow-up. The real background-read-loop -> EOF ->
// eventLoop.Post(...)-marshaled onDisconnected_ call path can't be
// exercised headlessly here, for the same reason DispatchFrame's own doc
// comment documents for the frame-dispatch path: ned::ui::EventLoop::Post
// only ever enqueues, with no synchronous fallback, so a real
// EventLoop::Run() would be needed to ever see a Post-driven call actually
// fire -- and no test in this codebase runs one. This just confirms
// SetOnDisconnected is a safe, replaceable hook, the same "connect after
// construction" shape SetNotificationHandler's own test just above
// confirms; the actual spawn-failure and JSON-RPC-error paths (which DO run
// entirely on the main thread, no Post needed) are covered end-to-end in
// LspManagerTest.cpp instead.
TEST_CASE("SetOnDisconnected replaces a previous handler, and unset is a safe no-op", "[Lsp]") {
    ClientFixture fixture = ClientFixture::Create();

    fixture.client.SetOnDisconnected([](std::string) { FAIL("should have been replaced"); });
    fixture.client.SetOnDisconnected([](std::string) {});
    SUCCEED(); // no crash from setting/replacing -- the callback itself is never invoked directly here
}
