#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "Editor/BackgroundActivity.h"
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
// Notcurses' EventLoop constructor calls notcurses_core_init immediately,
// entering the alternate screen buffer for real the instant one exists.
// Owned here, by value, as this fixture's own
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

    ClientFixture(int readFd, int writeFd, Transport transport, bool startHandshakeComplete) : serverStdinRead(readFd), serverStdoutWrite(writeFd), client(std::move(transport), eventLoop, startHandshakeComplete) {
    }

    ~ClientFixture() {
        ::close(serverStdoutWrite);
        ::close(serverStdinRead);
    }

    ClientFixture(const ClientFixture&)            = delete;
    ClientFixture& operator=(const ClientFixture&) = delete;

    static ClientFixture Create() {
        return CreateImpl(/*startHandshakeComplete=*/true);
    }

    // handshake-ordering follow-up: a fixture that starts *gated*, for tests
    // exercising SendRequest/SendNotification's own queue-until-initialized
    // behavior directly -- see LspClient.h's own doc comment on the
    // startHandshakeComplete constructor parameter this threads through to.
    static ClientFixture CreateGated() {
        return CreateImpl(/*startHandshakeComplete=*/false);
    }

  private:
    static ClientFixture CreateImpl(bool startHandshakeComplete) {
        int clientWritesHere[2]; // client's write end -> test's read end
        int clientReadsHere[2];  // test's write end -> client's read end
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        return ClientFixture(clientWritesHere[0], clientReadsHere[1], Transport(clientReadsHere[0], clientWritesHere[1]), startHandshakeComplete);
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

// handshake-ordering follow-up: ReadRawFrame above reads a fixed number of
// chunks and trusts each call starts at a fresh frame boundary -- true for
// every other test here (each sends one message, then reads once), but not
// for the "flush queued messages in order" test, which sends three
// SendNotification calls back-to-back before ever reading: all three land
// in the pipe together, so a single ::read() can return more than one
// frame's worth at once, and ReadRawFrame's own Content-Length check only
// looks for "at least the first frame," not "exactly." This reads
// everything available once, then splits it into count discrete frames by
// walking each one's own Content-Length in turn -- for that one test only.
std::vector<Json> ReadQueuedFrames(int fd, std::size_t count) {
    std::string all;
    char        buffer[1024];
    while (true) { // read until every frame is present
        std::size_t      framesSoFar = 0;
        std::string_view remaining(all);
        while (true) {
            const auto headerEnd = remaining.find("\r\n\r\n");
            if (headerEnd == std::string_view::npos) {
                break;
            }
            constexpr std::string_view kPrefix   = "Content-Length: ";
            const auto                 prefixPos = remaining.find(kPrefix);
            if (prefixPos == std::string_view::npos || prefixPos > headerEnd) {
                break;
            }
            const std::size_t contentLength = std::stoul(std::string(remaining.substr(prefixPos + kPrefix.size())));
            const std::size_t frameLength   = headerEnd + 4 + contentLength;
            if (remaining.size() < frameLength) {
                break;
            }
            ++framesSoFar;
            remaining = remaining.substr(frameLength);
        }
        if (framesSoFar >= count) {
            break;
        }
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
    }

    std::vector<Json> frames;
    std::string_view  remaining(all);
    for (std::size_t i = 0; i < count; ++i) {
        const auto                 headerEnd     = remaining.find("\r\n\r\n");
        constexpr std::string_view kPrefix       = "Content-Length: ";
        const auto                 prefixPos     = remaining.find(kPrefix);
        const std::size_t          contentLength = std::stoul(std::string(remaining.substr(prefixPos + kPrefix.size())));
        const std::string_view     body          = remaining.substr(headerEnd + 4, contentLength);
        frames.push_back(Json::parse(body));
        remaining = remaining.substr(headerEnd + 4 + contentLength);
    }
    return frames;
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

TEST_CASE("LspClient::ExpireStaleRequests resolves a stuck request with a synthetic timeout error", "[Lsp]") {
    // subprocess-hang-protection follow-up.
    ClientFixture fixture = ClientFixture::Create();

    bool                invoked = false;
    std::optional<Json> gotResult;
    std::optional<Json> gotError;
    fixture.client.SendRequest("textDocument/hover", Json::object(), [&](std::optional<Json> result, std::optional<Json> error) {
        invoked   = true;
        gotResult = result;
        gotError  = error;
    });
    REQUIRE_FALSE(invoked);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    fixture.client.ExpireStaleRequests(std::chrono::milliseconds(1));

    REQUIRE(invoked);
    REQUIRE_FALSE(gotResult.has_value());
    REQUIRE(gotError.has_value());
    REQUIRE((*gotError)["code"] == -32001);
}

TEST_CASE("LspClient::ExpireStaleRequests leaves a request younger than maxAge untouched", "[Lsp]") {
    // subprocess-hang-protection follow-up.
    ClientFixture fixture = ClientFixture::Create();

    bool invoked = false;
    fixture.client.SendRequest("textDocument/hover", Json::object(), [&](std::optional<Json>, std::optional<Json>) { invoked = true; });

    fixture.client.ExpireStaleRequests(std::chrono::milliseconds(60000)); // real request is only milliseconds old

    REQUIRE_FALSE(invoked);
}

TEST_CASE("LspClient::ExpireStaleRequests ends the LSP background-activity spinner for an expired request", "[Lsp]") {
    // subprocess-hang-protection follow-up: mirrors DispatchFrame's own
    // Begin/End pairing -- an expired request must not leave the mode-line
    // spinner running forever, same as a real response would.
    ClientFixture fixture = ClientFixture::Create();

    const std::size_t before = ned::editor::ActiveBackgroundActivities().size();
    fixture.client.SendRequest("textDocument/hover", Json::object(), [](std::optional<Json>, std::optional<Json>) {});
    REQUIRE(ned::editor::ActiveBackgroundActivities().size() == before + 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    fixture.client.ExpireStaleRequests(std::chrono::milliseconds(1));

    REQUIRE(ned::editor::ActiveBackgroundActivities().size() == before);
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

// lsp-use-after-free follow-up. Confirmed live via ASan: a real SIGSEGV/
// heap-use-after-free with LspManager destroying an LspClient (on a
// respawn-after-disconnect) while the background read thread's own already-
// Post()ed disconnect notification for that exact instance was still
// sitting in EventLoop's queue -- two independent background threads (this
// one, and LspManager's periodic maintenance tick) Post() with no ordering
// guarantee between them, so "destroy it a little later" is not actually
// safe at any delay. The fix lives in LspClient itself (alive_, see its own
// header comment) rather than in whoever owns it. This is the one test in
// this file that runs a real background-read-loop -> Post() -> drain cycle
// (see "SetOnDisconnected replaces a previous handler..." above for why
// every other test here avoids it) -- specifically to prove this fix, not
// just that the hook is replaceable.
TEST_CASE("A stray Post()ed callback safely no-ops instead of touching an already-destroyed LspClient", "[Lsp]") {
    ned::ui::EventLoop eventLoop;
    int                 clientWritesHere[2]; // client's write end -> test's read end
    int                 clientReadsHere[2];  // test's write end -> client's read end
    REQUIRE(::pipe(clientWritesHere) == 0);
    REQUIRE(::pipe(clientReadsHere) == 0);
    const int serverStdinRead   = clientWritesHere[0];
    const int serverStdoutWrite = clientReadsHere[1];

    std::optional<LspClient> client;
    client.emplace(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop, /*startHandshakeComplete=*/true);
    client->SetOnDisconnected([](std::string) {}); // present and callable, matching a real wired client

    ::close(serverStdoutWrite); // EOF -- the read thread Post()s its disconnect notification, then exits
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the background thread actually post before destroying

    client.reset(); // ~LspClient() flips alive_ to false as its first statement

    // The disconnect notification posted above is still queued. Draining it
    // must not crash or touch freed memory -- that's the entire point.
    eventLoop.DrainPosted_();
    SUCCEED();

    ::close(serverStdinRead);
}

TEST_CASE("LspClient counts an in-flight request as LSP background activity until its response dispatches", "[Lsp]") {
    auto fixture = ClientFixture::Create();
    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());

    fixture.client.SendRequest("textDocument/hover", Json::object(), [](std::optional<Json>, std::optional<Json>) {});
    const auto active = ned::editor::ActiveBackgroundActivities();
    REQUIRE(active.size() == 1);
    REQUIRE(active[0].name == "LSP");

    const Json response = {{"jsonrpc", "2.0"}, {"id", 1}, {"result", nullptr}}; // ids start at 1 on a fresh client
    fixture.client.DispatchFrame(response.dump());
    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());
}

TEST_CASE("LspClient's destructor ends the LSP background activity of requests never answered", "[Lsp]") {
    {
        auto fixture = ClientFixture::Create();
        fixture.client.SendRequest("textDocument/hover", Json::object(), [](std::optional<Json>, std::optional<Json>) {});
        fixture.client.SendRequest("textDocument/completion", Json::object(), [](std::optional<Json>, std::optional<Json>) {});
        REQUIRE(ned::editor::ActiveBackgroundActivities().size() == 1);
    }
    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());
}

TEST_CASE("LspClient answers a server-initiated request via its registered handler", "[Lsp]") {
    auto fixture = ClientFixture::Create();
    fixture.client.SetRequestHandler("window/workDoneProgress/create", [](const Json&) { return Json(nullptr); });

    const Json request = {{"jsonrpc", "2.0"}, {"id", 42}, {"method", "window/workDoneProgress/create"}, {"params", {{"token", "t"}}}};
    fixture.client.DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(fixture.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 42);
    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].is_null());
}

TEST_CASE("LspClient answers an unhandled server-initiated request with MethodNotFound", "[Lsp]") {
    auto fixture = ClientFixture::Create();

    const Json request = {{"jsonrpc", "2.0"}, {"id", 7}, {"method", "workspace/configuration"}, {"params", Json::object()}};
    fixture.client.DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(fixture.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 7);
    REQUIRE(response["error"]["code"] == -32601);
}

// handshake-ordering follow-up. Found live against a real harper-ls: a
// caller (LspManager::SyncBuffer's didOpen chief among them) that calls
// SendNotification/SendRequest on a freshly spawned client races ahead of
// the initialize response, which only arrives on a later event-loop
// iteration -- per spec nothing but the initialize request itself may be
// sent before "initialized" goes out, and harper-ls enforces this exactly:
// a didOpen that arrives first is silently never checked, with no error to
// explain why. clangd tolerates the same violation; harper-ls does not.
// These tests exercise the gate ClientFixture::CreateGated's own doc
// comment describes.

TEST_CASE("A gated LspClient queues SendNotification instead of writing it immediately", "[Lsp]") {
    ClientFixture fixture = ClientFixture::CreateGated();

    fixture.client.SendNotification("textDocument/didOpen", Json{{"marker", "queued"}});

    pollfd pfd{.fd = fixture.serverStdinRead, .events = POLLIN, .revents = 0};
    REQUIRE(::poll(&pfd, 1, 200) == 0); // nothing written yet -- still queued
}

TEST_CASE("A gated LspClient lets the initialize request itself through immediately", "[Lsp]") {
    ClientFixture fixture = ClientFixture::CreateGated();

    fixture.client.SendRequest("initialize", Json::object(), [](std::optional<Json>, std::optional<Json>) {});

    const std::string raw     = ReadRawFrame(fixture.serverStdinRead);
    const Json        message = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(message["method"] == "initialize");
}

TEST_CASE("Sending initialized flushes everything queued ahead of it, in order", "[Lsp]") {
    ClientFixture fixture = ClientFixture::CreateGated();

    fixture.client.SendNotification("textDocument/didOpen", Json{{"marker", "first"}});
    fixture.client.SendNotification("textDocument/didChange", Json{{"marker", "second"}});
    fixture.client.SendNotification("initialized", Json::object()); // opens the gate

    const std::vector<Json> frames = ReadQueuedFrames(fixture.serverStdinRead, 3);
    REQUIRE(frames[0]["method"] == "initialized"); // the notification that opened the gate is written first
    REQUIRE(frames[1]["method"] == "textDocument/didOpen");
    REQUIRE(frames[1]["params"]["marker"] == "first");
    REQUIRE(frames[2]["method"] == "textDocument/didChange");
    REQUIRE(frames[2]["params"]["marker"] == "second");
}

TEST_CASE("Once the gate is open, further calls write immediately with no more queuing", "[Lsp]") {
    ClientFixture fixture = ClientFixture::CreateGated();

    fixture.client.SendNotification("initialized", Json::object());
    (void)ReadRawFrame(fixture.serverStdinRead); // drain "initialized"

    fixture.client.SendNotification("textDocument/didOpen", Json::object());
    const std::string raw     = ReadRawFrame(fixture.serverStdinRead);
    const Json        message = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(message["method"] == "textDocument/didOpen"); // arrived without needing another "initialized"
}
