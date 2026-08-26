#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "Editor/BackgroundActivity.h"
#include "Editor/Lsp/LspBackgroundSync.h"
#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/LspServerConfig.h"
#include "Editor/Lsp/Transport.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::lsp::CodeAction;
using ned::editor::lsp::CompletionItem;
using ned::editor::lsp::Json;
using ned::editor::lsp::kProseLanguageKey;
using ned::editor::lsp::LspClient;
using ned::editor::lsp::LspManager;
using ned::editor::lsp::Transport;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// Mirrors LspClientTest.cpp's own ClientFixture exactly (see that file's
// header comment for the full rationale, including why serverStdoutWrite
// must be closed before the LspClient it feeds) -- a raw pipe pair standing
// in for a real language server's stdin/stdout, used here to drive
// LspManager::SetClientForTesting instead of LspClient directly.
struct FakeServer {
    int serverStdinRead;   // test reads what the client wrote
    int serverStdoutWrite; // test writes to feed the client's (unused, in these tests) read thread

    FakeServer(int readFd, int writeFd) : serverStdinRead(readFd), serverStdoutWrite(writeFd) {
    }

    ~FakeServer() {
        ::close(serverStdoutWrite);
        ::close(serverStdinRead);
    }

    FakeServer(const FakeServer&)            = delete;
    FakeServer& operator=(const FakeServer&) = delete;
    FakeServer(FakeServer&&)                 = default;

    static FakeServer Create(LspManager& manager, const std::string& language, ned::ui::EventLoop& eventLoop, LspClient*& outClient,
                             const Json& workspaceConfiguration = Json::object(), bool brokerBacked = false) {
        int clientWritesHere[2]; // client's write end -> test's read end
        int clientReadsHere[2];  // test's write end -> client's read end
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<LspClient>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient   = &manager.SetClientForTesting(language, std::move(client), workspaceConfiguration, brokerBacked);
        return FakeServer(clientWritesHere[0], clientReadsHere[1]);
    }
};

// Reads exactly one LSP frame's raw bytes off a plain fd -- copied from
// LspClientTest.cpp's own ReadRawFrame (kept file-local here too rather than
// shared, matching that file's own "not worth a new dependency between the
// two for something this small" precedent elsewhere in this codebase).
std::string ReadRawFrame(int fd) {
    std::string all;
    char        buffer[512];
    for (int i = 0; i < 4; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
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

// graceful-lsp-shutdown follow-up: ReadRawFrame above assumes exactly one
// frame arrives per call, which breaks the moment a caller (Shutdown())
// writes two frames back-to-back before this test ever reads -- both can
// land in the same read() (LspManager::Shutdown's own shutdown+exit pair,
// tiny frames over a fast local pipe), and ReadRawFrame's substr-to-end
// parse would then choke on the second frame's own headers trailing the
// first frame's body. Splits every complete frame out of raw by walking
// Content-Length boundaries instead of assuming there's only one.
std::vector<Json> ParseAllFrames(const std::string& raw) {
    std::vector<Json> frames;
    std::size_t       pos = 0;
    while (true) {
        const std::size_t headerEnd = raw.find("\r\n\r\n", pos);
        if (headerEnd == std::string::npos) {
            break;
        }
        const std::string_view kPrefix   = "Content-Length: ";
        const std::size_t      prefixPos = raw.find(kPrefix, pos);
        if (prefixPos == std::string::npos || prefixPos > headerEnd) {
            break;
        }
        const std::size_t contentLength = std::stoul(raw.substr(prefixPos + kPrefix.size()));
        const std::size_t bodyStart     = headerEnd + 4;
        if (raw.size() < bodyStart + contentLength) {
            break; // frame not fully arrived yet
        }
        frames.push_back(Json::parse(raw.substr(bodyStart, contentLength)));
        pos = bodyStart + contentLength;
    }
    return frames;
}

// Reads until at least frameCount complete frames have arrived (per
// ParseAllFrames above) or the read loop runs dry.
std::string ReadRawFramesUntil(int fd, std::size_t frameCount) {
    std::string all;
    char        buffer[512];
    for (int i = 0; i < 8; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
        if (ParseAllFrames(all).size() >= frameCount) {
            break;
        }
    }
    return all;
}

int RequestIdFromFrame(const std::string& raw) {
    return Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["id"].get<int>();
}

// prose-checking follow-up: asserting "nothing was ever sent" can't use
// ReadRawFrame's own blocking ::read (it would hang forever on a fd that
// legitimately never gets written to -- the case under test). A short,
// bounded poll() is the deliberate exception to this file's otherwise
// blocking-read style, used only here.
bool NoFrameArrives(int fd) {
    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&pfd, 1, 200) == 0; // 0 == timed out, nothing readable
}

// diagnostics-debounce follow-up: HandlePublishDiagnostics no longer applies
// a publish synchronously -- it (re)arms a per-buffer DeadlineTimer (see
// LspServerConfig.h's LspDiagnosticsDebounceMs) whose fire is Post()ed onto
// eventLoop from a background thread. Polls DrainPosted_ until predicate is
// true or a generous deadline passes, the same real-timer idiom
// PtyProcessTest.cpp's own tests already use.
template <typename Predicate>
void WaitUntil(ned::ui::EventLoop& eventLoop, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        eventLoop.DrainPosted_();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// A plain byte-count check is a sufficient predicate whenever a publish
// changes the total, but not when one message is swapped for another at the
// same count (see "A second publish from one source replaces only that
// source's own diagnostics slice" below, which waits on message content
// instead).
void WaitForDiagnosticCount(ned::ui::EventLoop& eventLoop, const Buffer& buffer, std::size_t expectedCount) {
    WaitUntil(eventLoop, [&] { return buffer.Diagnostics().size() == expectedCount; });
}

} // namespace

TEST_CASE("LspManager::RequestHover resolves synchronously to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                       invoked = false;
    std::optional<std::string> gotText;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string> text) {
        invoked = true;
        gotText = text;
    });

    REQUIRE(invoked); // no client/pending I/O involved -- fires immediately
    REQUIRE_FALSE(gotText.has_value());
}

TEST_CASE("LspManager::RequestCompletion resolves synchronously to an empty list when the buffer was never synced",
          "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                        invoked = false;
    std::vector<CompletionItem> gotItems;
    manager.RequestCompletion(buffer, 0, [&](std::vector<CompletionItem> items) {
        invoked  = true;
        gotItems = std::move(items);
    });

    REQUIRE(invoked);
    REQUIRE(gotItems.empty());
}

TEST_CASE("LspManager::SyncBuffer is a no-op for a buffer with no associated path", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch"); // no path -- Buffer::Path() == nullopt

    ned::editor::lsp::SetLspServerCommand("test-lang", {"/bin/cat"});
    manager.SyncBuffer(buffer, "test-lang"); // must not spawn anything or crash

    bool invoked = false;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string>) { invoked = true; });
    REQUIRE(invoked); // still resolves synchronously -- SyncBuffer never actually opened it

    ned::editor::lsp::SetLspServerCommand("test-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::SyncBuffer is a no-op when nothing is configured for the buffer's language", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-test.txt");

    manager.SyncBuffer(buffer, "a-language-nothing-is-configured-for"); // must not crash

    bool invoked = false;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string>) { invoked = true; });
    REQUIRE(invoked);
}

TEST_CASE("LspManager::NotifyBufferClosed is a no-op for a buffer that was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    manager.NotifyBufferClosed(buffer); // must not crash
    SUCCEED();
}

TEST_CASE("LspManager::RequestHover round-trips a real request/response through an injected client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-test.txt");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    manager.SyncBuffer(buffer, "test-lang");    // sends didOpen -- gets bufferState_ to "opened"
    (void)ReadRawFrame(server.serverStdinRead); // drain the didOpen notification

    bool                       invoked = false;
    std::optional<std::string> gotText;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string> text) {
        invoked = true;
        gotText = text;
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/hover");
    REQUIRE(request["params"]["position"]["line"] == 0);
    REQUIRE(request["params"]["position"]["character"] == 0);

    const Json response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", {{"contents", "it's an int"}}}};
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotText.has_value());
    REQUIRE(*gotText == "it's an int");
}

TEST_CASE("LspManager::ExpireStaleRequests reaches an injected client's own pending request", "[Lsp]") {
    // subprocess-hang-protection follow-up: confirms the manager-level sweep
    // actually forwards to a real running client, not just LspClient's own
    // already-covered unit behavior.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-expire-test.txt");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool                       invoked = false;
    std::optional<std::string> gotText;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string> text) {
        invoked = true;
        gotText = text;
    });
    (void)ReadRawFrame(server.serverStdinRead); // drain the hover request itself
    REQUIRE_FALSE(invoked);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    manager.ExpireStaleRequests(std::chrono::milliseconds(1));

    REQUIRE(invoked);
    REQUIRE_FALSE(gotText.has_value()); // synthetic timeout resolves like any other error response
}

TEST_CASE("LspManager::RequestHover resolves to nullopt on a JSON-RPC error response", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-error-test.txt");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    std::optional<std::string> gotText;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string> text) { gotText = text; });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"error", {{"code", -32601}, {"message", "nope"}}}};
    client->DispatchFrame(response.dump());

    REQUIRE_FALSE(gotText.has_value());

    // error-visibility follow-up: the error's own "message" is logged, not
    // just silently collapsed into a nullopt result.
    Buffer* log = bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName));
    REQUIRE(log != nullptr);
    REQUIRE(log->Text().find("test-lang") != std::string::npos);
    REQUIRE(log->Text().find("nope") != std::string::npos);
}

TEST_CASE("LspManager::SyncBuffer reports a spawn failure via *lsp log* instead of throwing, "
          "and doesn't retry every frame",
          "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    // LspManagerTest-broker-hermeticity follow-up: without this, ClientForLanguage's
    // real spawn path tries the *real* broker socket first, and if any broker daemon
    // (this test's own past run, or another `ned` process) is already listening there,
    // the connect succeeds and the expected synchronous spawn failure never happens --
    // it only surfaces later, async, on the broker's own side, after this test's REQUIREs
    // have already run. Point at a path nothing will ever listen on instead.
    manager.SetBrokerSocketPathOverrideForTesting(std::filesystem::temp_directory_path() / "ned-lsp-manager-test-no-broker.sock");
    Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-spawn-fail-test.txt");

    ned::editor::lsp::SetLspServerCommand("spawn-fail-lang", {"/definitely/does/not/exist/ned-fake-lsp"});

    manager.SyncBuffer(buffer, "spawn-fail-lang"); // must not throw

    Buffer* log = bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName));
    REQUIRE(log != nullptr);
    REQUIRE(log->ReadOnly());
    REQUIRE(log->Text().find("spawn-fail-lang") != std::string::npos);
    REQUIRE(log->Text().find("ned-fake-lsp") != std::string::npos);
    const std::size_t lengthAfterFirstFailure = log->Text().size();

    manager.SyncBuffer(buffer, "spawn-fail-lang"); // same command as before -- must not log again
    REQUIRE(bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName))->Text().size() == lengthAfterFirstFailure);

    // Reconfiguring to a *different* (still-nonexistent) command lifts the
    // gate -- one more attempt, one more log line.
    ned::editor::lsp::SetLspServerCommand("spawn-fail-lang", {"/still/does/not/exist/ned-fake-lsp-2"});
    manager.SyncBuffer(buffer, "spawn-fail-lang");
    REQUIRE(bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName))->Text().size() > lengthAfterFirstFailure);

    ned::editor::lsp::SetLspServerCommand("spawn-fail-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::StatusForLanguage reports NotConfigured, Running, and SpawnFailed", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    using ned::editor::lsp::LspManager;
    // See the spawn-failure test above for why this is needed for hermeticity.
    manager.SetBrokerSocketPathOverrideForTesting(std::filesystem::temp_directory_path() / "ned-lsp-manager-test-no-broker.sock");

    REQUIRE(manager.StatusForLanguage("status-test-lang") == LspManager::LspStatus::NotConfigured);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty());
    REQUIRE(manager.DisconnectReason("status-test-lang").empty());

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "status-test-lang", eventLoop, client);
    REQUIRE(manager.StatusForLanguage("status-test-lang") == LspManager::LspStatus::Running);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty());

    ned::editor::lsp::SetLspServerCommand("status-fail-lang", {"/definitely/does/not/exist/ned-fake-lsp"});
    Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-status-test.txt");
    manager.SyncBuffer(buffer, "status-fail-lang");
    REQUIRE(manager.StatusForLanguage("status-fail-lang") == LspManager::LspStatus::SpawnFailed);
    REQUIRE(manager.StatusForLanguage("status-test-lang") == LspManager::LspStatus::Running); // unaffected by the other language's failure
    // mode-line-lsp-status-round-3 follow-up: the spawn exception's message
    // is retained for the mode line's detail text.
    REQUIRE(manager.SpawnFailureDetail("status-fail-lang").find("ned-fake-lsp") != std::string::npos);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty()); // unaffected by the other language's failure

    ned::editor::lsp::SetLspServerCommand("status-fail-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::ClientDisconnected removes the client and updates status on a real disconnect", "[Lsp]") {
    // lsp-use-after-free follow-up: confirmed live -- a real SIGSEGV/ASan
    // heap-use-after-free from LspClient's own background read thread
    // Post()ing a callback that outlived the object. The fix now lives in
    // LspClient itself (alive_, see LspClient.h's own header comment and
    // LspClientTest.cpp's "A stray Post()ed callback safely no-ops..." for
    // the test that actually exercises that race) rather than here --
    // ClientDisconnected went back to a plain, immediate clients_.erase()
    // once that was fixed at the source. An earlier version of this fix
    // tried deferring destruction here instead (a retired_ vector, drained
    // by a periodic tick) and was confirmed live to not actually be safe at
    // any delay -- LspClient's own periodic maintenance tick and a client's
    // background thread both Post() against EventLoop with no ordering
    // guarantee between them. This test just confirms the ordinary,
    // expected behavior: a real disconnect removes the client and updates
    // status, full stop.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    auto       server = std::make_optional<FakeServer>(FakeServer::Create(manager, "disconnect-test-lang", eventLoop, client));
    REQUIRE(client != nullptr);
    REQUIRE(manager.StatusForLanguage("disconnect-test-lang") == LspManager::LspStatus::Running);

    server.reset(); // closes the fake server's write end -- EOF, the real disconnect path
    WaitUntil(eventLoop, [&] { return manager.StatusForLanguage("disconnect-test-lang") != LspManager::LspStatus::Running; });
    REQUIRE(manager.StatusForLanguage("disconnect-test-lang") == LspManager::LspStatus::Disconnected);
}

TEST_CASE("LspManager::ClientDisconnected gives up after a burst of immediate disconnects (crash-loop guard)", "[Lsp]") {
    // crash-loop-respawn-guard follow-up: confirmed live -- a misconfigured
    // phpantom_lsp respawned thousands of times within about a second, since
    // nothing previously stood between one ClientDisconnected and the very
    // next SyncBuffer's respawn attempt. Simulates the same rapid-disconnect
    // shape (inject a client, immediately EOF it, repeat) without a real
    // subprocess at all.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    ned::editor::lsp::SetLspServerCommand("crashloop-lang", {"/definitely/does/not/exist/ned-crashloop-lsp"});

    for (int i = 0; i < 3; ++i) {
        LspClient* client = nullptr;
        {
            FakeServer server = FakeServer::Create(manager, "crashloop-lang", eventLoop, client);
            // FakeServer's destructor closes serverStdoutWrite here -- EOF,
            // which LspClient's own read loop reports as onDisconnected_.
        }
        WaitUntil(eventLoop, [&] { return manager.StatusForLanguage("crashloop-lang") != LspManager::LspStatus::Running; });
    }

    REQUIRE(manager.StatusForLanguage("crashloop-lang") == LspManager::LspStatus::SpawnFailed);
    REQUIRE(manager.SpawnFailureDetail("crashloop-lang").find("disconnects in a row") != std::string::npos);

    ned::editor::lsp::SetLspServerCommand("crashloop-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::RequestCompletion round-trips a real request/response through an injected client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-completion-test.txt");
    buffer.InsertAtPoint("foo");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool                        invoked = false;
    std::vector<CompletionItem> gotItems;
    manager.RequestCompletion(buffer, buffer.Point(), [&](std::vector<CompletionItem> items) {
        invoked  = true;
        gotItems = std::move(items);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/completion");
    REQUIRE(request["params"]["position"]["character"] == 3);
    // completion-context follow-up: every caller is a manual/explicit
    // trigger, never a specific tracked trigger character.
    REQUIRE(request["params"]["context"]["triggerKind"] == 1);

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"isIncomplete", false}, {"items", Json::array({{{"label", "foobar"}, {"insertText", "foobar"}}})}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotItems.size() == 1);
    REQUIRE(gotItems[0].label == "foobar");
}

TEST_CASE("LspManager routes a real publishDiagnostics notification into Buffer::Diagnostics()", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    const Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", "file://" + path.string()},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                        {"severity", 1},
                                        {"message", "syntax error"}}})}}},
    };
    client->DispatchFrame(notification.dump());
    WaitForDiagnosticCount(eventLoop, buffer, 1);

    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].message == "syntax error");
    REQUIRE(buffer.Diagnostics()[0].severity == Buffer::Diagnostic::Severity::Error);
}

TEST_CASE("A publishDiagnostics notification is not applied until the debounce delay elapses", "[Lsp]") {
    const int originalDebounceMs = ned::editor::lsp::LspDiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(100);

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-debounce-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    const Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", "file://" + path.string()},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                        {"severity", 1},
                                        {"message", "syntax error"}}})}}},
    };
    client->DispatchFrame(notification.dump());
    eventLoop.DrainPosted_();
    REQUIRE(buffer.Diagnostics().empty()); // still pending -- the debounce delay hasn't elapsed yet

    WaitForDiagnosticCount(eventLoop, buffer, 1);
    REQUIRE(buffer.Diagnostics().size() == 1);

    ned::editor::lsp::SetLspDiagnosticsDebounceMs(originalDebounceMs);
}

TEST_CASE("A rapid burst of publishes collapses into a single application using only the latest content", "[Lsp]") {
    const int originalDebounceMs = ned::editor::lsp::LspDiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(150);

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-burst-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code bad code bad code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    auto notificationFor = [&](const std::string& message) {
        return Json{
            {"jsonrpc", "2.0"},
            {"method", "textDocument/publishDiagnostics"},
            {"params",
             {{"uri", "file://" + path.string()},
              {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                            {"severity", 1},
                                            {"message", message}}})}}},
        };
    };

    // Three publishes in quick succession -- each one rearms the same
    // buffer-level debounce timer, so only the last should ever reach the
    // buffer, and only once, well after the burst.
    client->DispatchFrame(notificationFor("first pass").dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    client->DispatchFrame(notificationFor("second pass").dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    client->DispatchFrame(notificationFor("final pass").dump());
    eventLoop.DrainPosted_();
    REQUIRE(buffer.Diagnostics().empty()); // still coalescing -- none of the three has landed yet

    WaitUntil(eventLoop, [&] { return !buffer.Diagnostics().empty(); });
    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].message == "final pass");

    ned::editor::lsp::SetLspDiagnosticsDebounceMs(originalDebounceMs);
}

TEST_CASE("LspManager::RequestCodeActions sends the range and overlapping diagnostics, and round-trips a real response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-action-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");
    buffer.SetDiagnostics({
        Buffer::Diagnostic{.startByte = 0, .endByte = 3, .severity = Buffer::Diagnostic::Severity::Error, .message = "boom"},
    });

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                    invoked = false;
    std::vector<CodeAction> gotActions;
    manager.RequestCodeActions(buffer, 0, 3, [&](std::vector<CodeAction> actions) {
        invoked    = true;
        gotActions = std::move(actions);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/codeAction");
    REQUIRE(request["params"]["range"]["start"]["character"] == 0);
    REQUIRE(request["params"]["range"]["end"]["character"] == 3);
    REQUIRE(request["params"]["context"]["diagnostics"].size() == 1);
    REQUIRE(request["params"]["context"]["diagnostics"][0]["message"] == "boom");
    REQUIRE(request["params"]["context"]["diagnostics"][0]["severity"] == 1);

    const std::string ownUri   = request["params"]["textDocument"]["uri"].get<std::string>();
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"title", "Fix the boom"},
                                 {"edit",
                                  {{"changes",
                                    {{ownUri, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                                            {"newText", "good"}}})}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotActions.size() == 1);
    REQUIRE(gotActions[0].title == "Fix the boom");
    REQUIRE(gotActions[0].hasEdit);
    REQUIRE_FALSE(gotActions[0].touchesOtherFiles);
    REQUIRE(gotActions[0].edits.size() == 1);
    REQUIRE(gotActions[0].edits[0].newText == "good");
}

TEST_CASE("LspManager::RequestCodeActions resolves to an empty list when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                    invoked = false;
    std::vector<CodeAction> gotActions;
    manager.RequestCodeActions(buffer, 0, 0, [&](std::vector<CodeAction> actions) {
        invoked    = true;
        gotActions = std::move(actions);
    });

    REQUIRE(invoked);
    REQUIRE(gotActions.empty());
}

TEST_CASE("LspManager::ResolveCodeAction sends action.raw verbatim and returns the resolved edit", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-resolve-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    // A resolvable action (as ExtractCodeActions would have produced it):
    // "kind" present, no "edit" yet.
    const Json                         unresolved = {{"title", "Remove unused #include"}, {"kind", "quickfix"}, {"data", {{"opaque", 7}}}};
    const ned::editor::lsp::CodeAction action     = ned::editor::lsp::ExtractSingleCodeAction(unresolved, ownUri);
    REQUIRE(action.resolvable);

    bool                                        invoked = false;
    std::optional<ned::editor::lsp::CodeAction> gotResolved;
    manager.ResolveCodeAction(buffer, action, [&](std::optional<ned::editor::lsp::CodeAction> resolved) {
        invoked     = true;
        gotResolved = std::move(resolved);
    });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        sentBack = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["params"];
    REQUIRE(sentBack == unresolved); // the exact original item, round-tripped verbatim

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"title", "Remove unused #include"}, {"kind", "quickfix"}, {"edit", {{"changes", {{ownUri, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}}, {"newText", ""}}})}}}}}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotResolved.has_value());
    REQUIRE(gotResolved->hasEdit);
    REQUIRE(gotResolved->edits.size() == 1);
}

TEST_CASE("LspManager::ResolveCodeAction resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    ned::editor::lsp::CodeAction action;
    action.title      = "Fix";
    action.resolvable = true;
    action.raw        = Json{{"title", "Fix"}, {"kind", "quickfix"}};

    bool                                        invoked = false;
    std::optional<ned::editor::lsp::CodeAction> gotResolved;
    manager.ResolveCodeAction(buffer, action, [&](std::optional<ned::editor::lsp::CodeAction> resolved) {
        invoked     = true;
        gotResolved = resolved;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(gotResolved.has_value());
}

TEST_CASE("LspManager::RequestCodeActions with a serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-code-action-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("teh");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "test-lang", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(kProseLanguageKey), eventLoop, proseClient);

    manager.SyncBuffer(buffer, "test-lang"); // syncs both the primary language server and the prose connection
    (void)ReadRawFrame(primaryServer.serverStdinRead);
    (void)ReadRawFrame(proseServer.serverStdinRead);

    bool                    invoked = false;
    std::vector<CodeAction> gotActions;
    manager.RequestCodeActions(
        buffer, 0, 3, [&](std::vector<CodeAction> actions) {
            invoked    = true;
            gotActions = std::move(actions);
        },
        std::string(kProseLanguageKey));

    REQUIRE(NoFrameArrives(primaryServer.serverStdinRead)); // never asked the primary language server

    const std::string raw     = ReadRawFrame(proseServer.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/codeAction");

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"title", "Add to dictionary"}, {"command", "HarperAddToUserDict"}, {"arguments", Json::array()}}})},
    };
    proseClient->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotActions.size() == 1);
    REQUIRE(gotActions[0].title == "Add to dictionary");
    REQUIRE(gotActions[0].command.has_value());
    REQUIRE(gotActions[0].command->name == "HarperAddToUserDict");
}

TEST_CASE("LspManager::ExecuteCommand sends workspace/executeCommand and reports ok on a real response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-execute-command-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool invoked = false;
    bool gotOk   = false;
    manager.ExecuteCommand(buffer, {}, "HarperAddToUserDict", Json::array({"teh"}), [&](bool ok) {
        invoked = true;
        gotOk   = ok;
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "workspace/executeCommand");
    REQUIRE(request["params"]["command"] == "HarperAddToUserDict");
    REQUIRE(request["params"]["arguments"] == Json::array({"teh"}));

    const Json response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", Json(nullptr)}};
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotOk);
}

TEST_CASE("LspManager::ExecuteCommand reports failure on a JSON-RPC error response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-execute-command-error-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool gotOk = true;
    manager.ExecuteCommand(buffer, {}, "unknown.command", Json::array(), [&](bool ok) { gotOk = ok; });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"error", {{"code", -32601}, {"message", "unknown command"}}}};
    client->DispatchFrame(response.dump());

    REQUIRE_FALSE(gotOk);
}

TEST_CASE("LspManager::ExecuteCommand reports failure when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool invoked = false;
    bool gotOk   = true;
    manager.ExecuteCommand(buffer, {}, "whatever", Json::array(), [&](bool ok) {
        invoked = true;
        gotOk   = ok;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(gotOk);
}

TEST_CASE("LspManager::RequestDefinition resolves a Location-array response's uris to real paths", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestDefinition(buffer, 0, [&](std::vector<LspManager::ResolvedLocation> locations) {
        invoked = true;
        got     = std::move(locations);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/definition");

    const std::filesystem::path definitionPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-target.txt";
    const Json                  response       = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"uri", "file://" + definitionPath.string()},
                                 {"range", {{"start", {{"line", 3}, {"character", 7}}}, {"end", {{"line", 3}, {"character", 11}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == definitionPath);
    REQUIRE(got[0].position.line == 3);
    REQUIRE(got[0].position.character == 7);
}

TEST_CASE("LspManager::RequestDefinition resolves to an empty list when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestDefinition(buffer, 0, [&](std::vector<LspManager::ResolvedLocation> locations) {
        invoked = true;
        got     = std::move(locations);
    });

    REQUIRE(invoked);
    REQUIRE(got.empty());
}

// declaration/typeDefinition/implementation follow-up: one representative
// test per sibling request, confirming each sends its own distinct wire
// method and still resolves a response through the shared
// ExtractDefinitionLocations/uri-to-path path RequestDefinition's own test
// above already covers in full -- no need to re-test empty-result/unsynced
// cases three more times, that logic is shared (SendLocationRequest), not
// duplicated per method.
TEST_CASE("LspManager::RequestDeclaration sends textDocument/declaration and resolves a response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-declaration-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestDeclaration(buffer, 0, [&](std::vector<LspManager::ResolvedLocation> locations) {
        invoked = true;
        got     = std::move(locations);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/declaration");

    const std::filesystem::path targetPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-declaration-target.txt";
    const Json                  response   = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"uri", "file://" + targetPath.string()}, {"range", {{"start", {{"line", 1}, {"character", 0}}}, {"end", {{"line", 1}, {"character", 4}}}}}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == targetPath);
}

TEST_CASE("LspManager::RequestTypeDefinition sends textDocument/typeDefinition", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-typedefinition-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestTypeDefinition(buffer, 0, [](std::vector<LspManager::ResolvedLocation>) {});

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/typeDefinition");
}

TEST_CASE("LspManager::RequestImplementation sends textDocument/implementation", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-implementation-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestImplementation(buffer, 0, [](std::vector<LspManager::ResolvedLocation>) {});

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/implementation");
}

// find-references follow-up: same "sends its own distinct wire method"
// shape as the three tests above, plus the one thing that's actually unique
// to this request -- a "context": {"includeDeclaration": true} field none
// of the other three location requests send.
TEST_CASE("LspManager::RequestReferences sends textDocument/references with includeDeclaration and resolves a response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-references-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestReferences(buffer, 0, [&](std::vector<LspManager::ResolvedLocation> locations) {
        invoked = true;
        got     = std::move(locations);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/references");
    REQUIRE(request["params"]["context"]["includeDeclaration"] == true);

    const std::filesystem::path targetPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-references-target.txt";
    const Json                  response   = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"uri", "file://" + targetPath.string()},
                                 {"range", {{"start", {{"line", 2}, {"character", 3}}}, {"end", {{"line", 2}, {"character", 7}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == targetPath);
    REQUIRE(got[0].position.line == 2);
}

// symbol-search follow-up.
TEST_CASE("LspManager::RequestDocumentSymbols sends textDocument/documentSymbol and resolves its own uri to a path",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-docsymbol-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("struct Widget {};");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                  invoked = false;
    std::vector<LspManager::SymbolResult> got;
    manager.RequestDocumentSymbols(buffer, [&](std::vector<LspManager::SymbolResult> symbols) {
        invoked = true;
        got     = std::move(symbols);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/documentSymbol");
    REQUIRE(request["params"].contains("textDocument"));
    REQUIRE_FALSE(request["params"].contains("position")); // no position for this request, unlike hover/definition/etc.

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"name", "Widget"},
                                 {"kind", 23},
                                 {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 18}}}}},
                                 {"selectionRange",
                                  {{"start", {{"line", 0}, {"character", 7}}}, {"end", {{"line", 0}, {"character", 13}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].name == "Widget");
    REQUIRE(got[0].kind == 23);
    REQUIRE(got[0].path == path);
    REQUIRE(got[0].position.character == 7);
}

TEST_CASE("LspManager::RequestWorkspaceSymbols sends workspace/symbol with the query and no textDocument field",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-wssymbol-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("x");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool                                  invoked = false;
    std::vector<LspManager::SymbolResult> got;
    manager.RequestWorkspaceSymbols(buffer, "Wid", [&](std::vector<LspManager::SymbolResult> symbols) {
        invoked = true;
        got     = std::move(symbols);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "workspace/symbol");
    REQUIRE(request["params"]["query"] == "Wid");
    REQUIRE_FALSE(request["params"].contains("textDocument"));

    const std::filesystem::path resultPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-wssymbol-result-test.cpp";
    const Json                  response   = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"name", "Widget"},
                                 {"kind", 5},
                                 {"containerName", "ui"},
                                 {"location",
                                  {{"uri", "file://" + resultPath.string()},
                                   {"range", {{"start", {{"line", 4}, {"character", 0}}}, {"end", {{"line", 4}, {"character", 6}}}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].name == "Widget");
    REQUIRE(got[0].containerName == "ui");
    REQUIRE(got[0].path == resultPath);
    REQUIRE(got[0].position.line == 4);
}

TEST_CASE("LspManager::RequestSignatureHelp sends textDocument/signatureHelp and resolves the formatted text", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-signature-help-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("foo(");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                       invoked = false;
    std::optional<std::string> got;
    manager.RequestSignatureHelp(buffer, buffer.Point(), [&](std::optional<std::string> text) {
        invoked = true;
        got     = std::move(text);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/signatureHelp");

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"signatures", Json::array({{{"label", "foo(a: int)"}, {"parameters", Json::array({{{"label", "a: int"}}})}}})}, {"activeParameter", 0}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(*got == "foo(**a: int**)");
}

TEST_CASE("LspManager::RequestSignatureHelp resolves nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                       invoked = false;
    std::optional<std::string> got;
    manager.RequestSignatureHelp(buffer, 0, [&](std::optional<std::string> text) {
        invoked = true;
        got     = std::move(text);
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("LspManager::RequestSwitchSourceHeader sends a bare TextDocumentIdentifier and resolves a string uri response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-switch-header-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                 invoked = false;
    std::optional<std::filesystem::path> got;
    manager.RequestSwitchSourceHeader(buffer, [&](std::optional<std::filesystem::path> path) {
        invoked = true;
        got     = path;
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/switchSourceHeader");
    REQUIRE(request["params"].contains("uri"));
    REQUIRE_FALSE(request["params"].contains("textDocument")); // bare TextDocumentIdentifier, not wrapped

    const std::filesystem::path headerPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-switch-header-test.h";
    const Json                  response   = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", "file://" + headerPath.string()},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(*got == headerPath);
}

TEST_CASE("LspManager::RequestSwitchSourceHeader resolves to nullopt on a null result (no counterpart)", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-switch-header-null-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                 invoked = false;
    std::optional<std::filesystem::path> got;
    manager.RequestSwitchSourceHeader(buffer, [&](std::optional<std::filesystem::path> path) {
        invoked = true;
        got     = path;
    });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", nullptr},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("LspManager::RequestSwitchSourceHeader resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                 invoked = false;
    std::optional<std::filesystem::path> got;
    manager.RequestSwitchSourceHeader(buffer, [&](std::optional<std::filesystem::path> path) {
        invoked = true;
        got     = path;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("LspManager::RequestRename sends newName and resolves a multi-file WorkspaceEdit's uris to real paths", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-rename-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("old_name");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    bool                                      invoked = false;
    std::optional<LspManager::ResolvedRename> got;
    manager.RequestRename(buffer, 0, "new_name", [&](std::optional<LspManager::ResolvedRename> result) {
        invoked = true;
        got     = std::move(result);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/rename");
    REQUIRE(request["params"]["newName"] == "new_name");

    const std::filesystem::path otherPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-rename-other.txt";
    const Json                  response  = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result",
         {{"changes",
           {
               {ownUri, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}},
                                      {"newText", "new_name"}}})},
               {"file://" + otherPath.string(),
                Json::array({{{"range", {{"start", {{"line", 1}, {"character", 2}}}, {"end", {{"line", 1}, {"character", 10}}}}},
                              {"newText", "new_name"}}})},
           }}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(got->hasEdit);
    REQUIRE_FALSE(got->touchesUnsupportedForm);
    REQUIRE(got->edits.size() == 2);
}

TEST_CASE("LspManager::RequestRename resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                      invoked = false;
    std::optional<LspManager::ResolvedRename> got;
    manager.RequestRename(buffer, 0, "new_name", [&](std::optional<LspManager::ResolvedRename> result) {
        invoked = true;
        got     = result;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("BuildInitializeParams advertises codeActionLiteralSupport alongside the resolve capabilities", "[Lsp]") {
    // Regression test: without codeActionLiteralSupport a spec-following
    // server (clangd included) may only return bare Command objects -- no
    // "edit", no "kind" -- so every "fix available" quickfix listed fine but
    // applied as "has no edit to apply". See BuildInitializeParams' own
    // comment in LspManager.cpp.
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"));

    REQUIRE(params["rootUri"] == "file:///some/project");
    REQUIRE(params["processId"].is_number_integer());

    const Json& codeAction = params.at("capabilities").at("textDocument").at("codeAction");
    const Json& valueSet   = codeAction.at("codeActionLiteralSupport").at("codeActionKind").at("valueSet");
    REQUIRE(valueSet.is_array());
    REQUIRE(std::find(valueSet.begin(), valueSet.end(), Json("quickfix")) != valueSet.end());

    // The pre-existing resolve capabilities must survive the restructuring.
    REQUIRE(codeAction.at("dataSupport") == true);
    REQUIRE(codeAction.at("resolveSupport").at("properties") == Json::array({"edit"}));

    // workDoneProgress-support follow-up: invites $/progress reporting.
    REQUIRE(params.at("capabilities").at("window").at("workDoneProgress") == true);
}

// capabilities-hygiene follow-up: regression test for the gap the LSP
// coverage survey found -- this client sends/handles hover, definition,
// declaration, typeDefinition, implementation, references, rename,
// signatureHelp, publishDiagnostics, workspace/configuration, and
// workspace/executeCommand, but previously declared capabilities for none
// of them (only completion/codeAction/window.workDoneProgress existed).
TEST_CASE("BuildInitializeParams declares capabilities for every request/notification this client actually sends", "[Lsp]") {
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"));

    const Json& textDocument = params.at("capabilities").at("textDocument");
    for (const char* key :
         {"hover", "signatureHelp", "declaration", "definition", "typeDefinition", "implementation", "references", "rename",
          "publishDiagnostics"}) {
        REQUIRE(textDocument.contains(key));
    }

    const Json& workspace = params.at("capabilities").at("workspace");
    REQUIRE(workspace.at("configuration") == true);
    REQUIRE(workspace.contains("didChangeConfiguration"));
    REQUIRE(workspace.contains("executeCommand"));
}

TEST_CASE("BuildInitializeParams absolutizes a relative rootUri", "[Lsp]") {
    // PathToUri (file-local in LspManager.cpp, reached through
    // BuildInitializeParams here) must never emit a relative file:// URI --
    // "file://demo.cpp" is unresolvable, and clangd rejects every request
    // naming one. A buffer opened via a relative CLI argument is the real
    // case; rootUri exercises the same helper.
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("relative/dir"));

    const std::string rootUri = params["rootUri"].get<std::string>();
    REQUIRE(rootUri.rfind("file:///", 0) == 0);
    REQUIRE(rootUri.find("relative/dir") != std::string::npos);
}

TEST_CASE("BuildInitializeParams sends rootUri null for an empty project root", "[Lsp]") {
    // A real SIGABRT from a core dump: an empty ProjectRoot() reached
    // PathToUri, whose absolute("") throws, with no catch anywhere above
    // the Paint()-driven handshake -- the whole editor aborted on first
    // paint. The LSP spec explicitly allows "rootUri: DocumentUri | null",
    // so an empty root degrades to null rather than throwing or emitting a
    // nonsense "file://" URI.
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path());

    REQUIRE(params.contains("rootUri"));
    REQUIRE(params["rootUri"].is_null());
}

TEST_CASE("BuildInitializeParams omits initializationOptions when none is given", "[Lsp]") {
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"));
    REQUIRE_FALSE(params.contains("initializationOptions"));
}

TEST_CASE("BuildInitializeParams merges a non-empty initializationOptions verbatim", "[Lsp]") {
    // project-settings-lsp-init-options follow-up: e.g. a PHP project that
    // always preloads a bootstrap file before any real request runs, and
    // needs its language server told about that file via whatever shape its
    // own initializationOptions schema expects -- BuildInitializeParams
    // itself is unopinionated about the contents, just merges them in.
    const Json options = Json{{"bootstrapFiles", Json::array({"bootstrap.php"})}};
    const Json params  = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"), options);

    REQUIRE(params.contains("initializationOptions"));
    REQUIRE(params["initializationOptions"] == options);
}

TEST_CASE("LspManager tracks $/progress begin/report/end as LSP background activity with detail", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());

    const Json begin = {{"jsonrpc", "2.0"},
                        {"method", "$/progress"},
                        {"params", {{"token", "backgroundIndexProgress"}, {"value", {{"kind", "begin"}, {"title", "indexing"}}}}}};
    client->DispatchFrame(begin.dump());
    auto active = ned::editor::ActiveBackgroundActivities();
    REQUIRE(active.size() == 1);
    REQUIRE(active[0].name == "LSP");
    REQUIRE(active[0].detail == "indexing");

    const Json report = {{"jsonrpc", "2.0"},
                         {"method", "$/progress"},
                         {"params", {{"token", "backgroundIndexProgress"}, {"value", {{"kind", "report"}, {"percentage", 45}}}}}};
    client->DispatchFrame(report.dump());
    active = ned::editor::ActiveBackgroundActivities();
    REQUIRE(active.size() == 1);
    REQUIRE(active[0].detail == "indexing (45%)");

    // A report for a token that never began must not resurrect anything later.
    const Json end = {{"jsonrpc", "2.0"},
                      {"method", "$/progress"},
                      {"params", {{"token", "backgroundIndexProgress"}, {"value", {{"kind", "end"}}}}}};
    client->DispatchFrame(end.dump());
    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());

    client->DispatchFrame(end.dump()); // duplicate end -- must clamp, not go negative
    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());
}

TEST_CASE("LspManager answers window/workDoneProgress/create with a null result", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "window/workDoneProgress/create"}, {"params", {{"token", "t"}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 3);
    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].is_null());
}

TEST_CASE("LspManager resolves workspace/configuration sections against lspWorkspaceConfiguration", "[Lsp]") {
    // project-settings-lsp-init-options follow-up: a config-pull server
    // (e.g. intelephense/phpactor-style) asks for its own section by dotted
    // path -- confirm both a top-level and a nested section resolve, and an
    // unconfigured one still falls back to null rather than erroring.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    const Json workspaceConfig = Json{{"phpactor", {{"file_extensions", Json::array({"php"})}}},
                                      {"intelephense", {{"environment", {{"includePaths", Json::array({"/stubs"})}}}}}};

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "php", eventLoop, client, workspaceConfig);

    const Json request = {{"jsonrpc", "2.0"},
                          {"id", 7},
                          {"method", "workspace/configuration"},
                          {"params",
                           {{"items", Json::array({{{"section", "phpactor"}}, {{"section", "intelephense.environment"}}, {{"section", "unconfigured.section"}}})}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 7);
    REQUIRE(response["result"].is_array());
    REQUIRE(response["result"].size() == 3);
    CHECK(response["result"][0] == Json{{"file_extensions", Json::array({"php"})}});
    CHECK(response["result"][1] == Json{{"includePaths", Json::array({"/stubs"})}});
    CHECK(response["result"][2].is_null());
}

TEST_CASE("LspManager answers workspace/configuration with null for every item when nothing is configured", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "workspace/configuration"}, {"params", {{"items", Json::array({{{"section", "anything"}}})}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["result"].is_array());
    REQUIRE(response["result"].size() == 1);
    CHECK(response["result"][0].is_null());
}

// prose-checking follow-up: the prose-checker connection is just another
// entry in the same clients_/bufferState_ maps under
// LspManager::kProseLanguageKey (see LspManager.h's own doc comment on that
// constant) -- SetClientForTesting works on it exactly like any other
// language, so these tests never touch ProseChecker.h's real
// auto-detect/enabled machinery at all (ProseCheckerTestGuard.cpp disables
// that globally for the whole ned_tests binary regardless).

TEST_CASE("SyncBuffer opens both the primary language server and the prose checker independently", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-sync-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("some text");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);

    manager.SyncBuffer(buffer, "markdown");

    const std::string primaryRaw  = ReadRawFrame(primaryServer.serverStdinRead);
    const Json        primaryOpen = Json::parse(primaryRaw.substr(primaryRaw.find("\r\n\r\n") + 4));
    REQUIRE(primaryOpen["method"] == "textDocument/didOpen");
    REQUIRE(primaryOpen["params"]["textDocument"]["languageId"] == "markdown");

    const std::string proseRaw  = ReadRawFrame(proseServer.serverStdinRead);
    const Json        proseOpen = Json::parse(proseRaw.substr(proseRaw.find("\r\n\r\n") + 4));
    REQUIRE(proseOpen["method"] == "textDocument/didOpen");
    // The prose checker's own didOpen carries the buffer's real language as
    // languageId, not the reserved "prose" server key -- harper-ls needs the
    // real language to know how to extract comments/strings from a document.
    REQUIRE(proseOpen["params"]["textDocument"]["languageId"] == "markdown");
}

TEST_CASE("Diagnostics published by the primary language server and the prose checker both land in Buffer::Diagnostics()",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-merge-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code teh");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);
    manager.SyncBuffer(buffer, "markdown");
    (void)ReadRawFrame(primaryServer.serverStdinRead); // drain didOpen
    (void)ReadRawFrame(proseServer.serverStdinRead);

    const std::string uri                = "file://" + path.string();
    const Json        primaryDiagnostics = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", uri},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                        {"severity", 1},
                                        {"message", "syntax error"}}})}}},
    };
    primaryClient->DispatchFrame(primaryDiagnostics.dump());

    const Json proseDiagnostics = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", uri},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 9}}}, {"end", {{"line", 0}, {"character", 12}}}}},
                                        {"severity", 4},
                                        {"message", "possible typo: teh"}}})}}},
    };
    proseClient->DispatchFrame(proseDiagnostics.dump());
    WaitForDiagnosticCount(eventLoop, buffer, 2);

    REQUIRE(buffer.Diagnostics().size() == 2); // neither server's publish clobbered the other's
    bool sawSyntaxError = false;
    bool sawTypo        = false;
    for (const Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        if (diagnostic.message == "syntax error") {
            sawSyntaxError = true;
            // prose-diagnostic-callout follow-up: the real ("markdown") server's
            // own diagnostic must stay tagged Code -- BufferView renders that
            // origin with the ordinary underline/inline-annotation treatment.
            REQUIRE(diagnostic.origin == Buffer::Diagnostic::Origin::Code);
        }
        else if (diagnostic.message == "possible typo: teh") {
            sawTypo = true;
            // The prose checker's own connection is keyed by kProseLanguageKey
            // regardless of the buffer's real language -- see
            // HandlePublishDiagnostics' own origin derivation.
            REQUIRE(diagnostic.origin == Buffer::Diagnostic::Origin::Prose);
        }
    }
    REQUIRE(sawSyntaxError);
    REQUIRE(sawTypo);
}

TEST_CASE("A second publish from one source replaces only that source's own diagnostics slice", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-reslice-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code teh");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);
    manager.SyncBuffer(buffer, "markdown");
    (void)ReadRawFrame(primaryServer.serverStdinRead);
    (void)ReadRawFrame(proseServer.serverStdinRead);

    const std::string uri                     = "file://" + path.string();
    auto              diagnosticsNotification = [&](const std::string& message) {
        return Json{
            {"jsonrpc", "2.0"},
            {"method", "textDocument/publishDiagnostics"},
            {"params",
             {{"uri", uri},
              {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                            {"severity", 1},
                                            {"message", message}}})}}},
        };
    };

    primaryClient->DispatchFrame(diagnosticsNotification("first error").dump());
    proseClient->DispatchFrame(diagnosticsNotification("possible typo: teh").dump());
    WaitForDiagnosticCount(eventLoop, buffer, 2);
    REQUIRE(buffer.Diagnostics().size() == 2);

    // Primary republishes its own full current set (a real server does this
    // on every didChange) -- only its own slice is replaced, the prose
    // checker's diagnostic from before must survive untouched.
    primaryClient->DispatchFrame(diagnosticsNotification("second error").dump());
    // Total count stays 2 across this replacement (one message swapped for
    // another), so WaitForDiagnosticCount's own count check can't detect
    // when the debounced application has actually landed -- wait on the
    // new message's content instead.
    WaitUntil(eventLoop, [&] {
        return std::any_of(buffer.Diagnostics().begin(), buffer.Diagnostics().end(),
                           [](const Buffer::Diagnostic& d) { return d.message == "second error"; });
    });

    REQUIRE(buffer.Diagnostics().size() == 2);
    bool sawSecondError = false;
    bool sawFirstError  = false;
    bool sawTypo        = false;
    for (const Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        sawSecondError |= diagnostic.message == "second error";
        sawFirstError |= diagnostic.message == "first error";
        sawTypo |= diagnostic.message == "possible typo: teh";
    }
    REQUIRE(sawSecondError);
    REQUIRE_FALSE(sawFirstError); // primary's own stale diagnostic is gone
    REQUIRE(sawTypo);             // prose's diagnostic from before is untouched
}

TEST_CASE("NotifyBufferClosed sends didClose to every server the buffer was opened with", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-close-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("some text");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);
    manager.SyncBuffer(buffer, "markdown");
    (void)ReadRawFrame(primaryServer.serverStdinRead); // drain didOpen
    (void)ReadRawFrame(proseServer.serverStdinRead);

    manager.NotifyBufferClosed(buffer);

    const std::string primaryRaw   = ReadRawFrame(primaryServer.serverStdinRead);
    const Json        primaryClose = Json::parse(primaryRaw.substr(primaryRaw.find("\r\n\r\n") + 4));
    REQUIRE(primaryClose["method"] == "textDocument/didClose");

    const std::string proseRaw   = ReadRawFrame(proseServer.serverStdinRead);
    const Json        proseClose = Json::parse(proseRaw.substr(proseRaw.find("\r\n\r\n") + 4));
    REQUIRE(proseClose["method"] == "textDocument/didClose");
}

TEST_CASE("SyncBuffer never opens the prose checker for a binary buffer, but the primary language server still opens normally",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-binary-test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out.put('\0');
        out << "some content after a nul byte";
    }
    Buffer& buffer = bufferList.OpenOrCreateFile(path, /*allowBinary=*/true);

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "fundamental", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);

    manager.SyncBuffer(buffer, "fundamental");

    // The binary skip is scoped to the prose checker only -- the primary
    // language server still opens the buffer exactly as it always has.
    const std::string primaryRaw = ReadRawFrame(primaryServer.serverStdinRead);
    REQUIRE(primaryRaw.find("textDocument/didOpen") != std::string::npos);

    REQUIRE(NoFrameArrives(proseServer.serverStdinRead));
}

// embedded-language-documents follow-up: below this point, tests for
// SyncEmbeddedDocuments, the PrimarySyncState fix it required, diagnostics
// filtering by owned range, and serverKey routing on the four requests that
// previously only ever resolved to PrimarySyncState.

TEST_CASE("SyncEmbeddedDocuments opens an embedded server independently of the primary, sending the given text verbatim",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-open-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    (void)ReadRawFrame(htmlServer.serverStdinRead); // drain html's own didOpen

    REQUIRE(NoFrameArrives(jsServer.serverStdinRead)); // not yet embedded-synced

    const std::string paddedText = "        let x = 1;          "; // stands in for real padding -- content unimportant here
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = paddedText, .ownedRanges = {{8, 19}}}});

    const std::string raw    = ReadRawFrame(jsServer.serverStdinRead);
    const Json        opened = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(opened["method"] == "textDocument/didOpen");
    REQUIRE(opened["params"]["textDocument"]["text"] == paddedText);
    REQUIRE(opened["params"]["textDocument"]["languageId"] == "javascript");

    const auto activeKeys = manager.ActiveServerKeysForBuffer(buffer);
    REQUIRE(std::find(activeKeys.begin(), activeKeys.end(), "html") != activeKeys.end());
    REQUIRE(std::find(activeKeys.begin(), activeKeys.end(), "javascript") != activeKeys.end());
}

TEST_CASE("SyncEmbeddedDocuments tears down a server key whose region disappeared: didClose sent, its diagnostics dropped",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-teardown-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
    (void)ReadRawFrame(jsServer.serverStdinRead); // drain didOpen

    // A javascript diagnostic lands while the region still exists.
    const Json diagnosticsParams = {
        {"uri", "file://" + path.string()},
        {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                      {"severity", 1},
                                      {"message", "unused variable"}}})},
    };
    jsClient->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", diagnosticsParams}}.dump());
    WaitForDiagnosticCount(eventLoop, buffer, 1);

    // The <script> block is gone -- the next sync reports no javascript document at all.
    manager.SyncEmbeddedDocuments(buffer, {});

    const std::string closeRaw = ReadRawFrame(jsServer.serverStdinRead);
    const Json        closed   = Json::parse(closeRaw.substr(closeRaw.find("\r\n\r\n") + 4));
    REQUIRE(closed["method"] == "textDocument/didClose");

    WaitForDiagnosticCount(eventLoop, buffer, 0); // the stale javascript diagnostic must not linger

    const auto activeKeys = manager.ActiveServerKeysForBuffer(buffer);
    REQUIRE(std::find(activeKeys.begin(), activeKeys.end(), "javascript") == activeKeys.end());
    REQUIRE(std::find(activeKeys.begin(), activeKeys.end(), "html") != activeKeys.end()); // primary untouched
}

TEST_CASE(
    "PrimarySyncState fix regression: with host, prose, and an embedded key all synced, a default-serverKey request still "
    "resolves to the host server",
    "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-primary-ambiguity-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient  = nullptr;
    LspClient* proseClient = nullptr;
    LspClient* jsClient    = nullptr;
    FakeServer htmlServer  = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer proseServer = FakeServer::Create(manager, std::string(kProseLanguageKey), eventLoop, proseClient);
    FakeServer jsServer    = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html"); // primary ("html") + prose
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    (void)ReadRawFrame(proseServer.serverStdinRead);
    (void)ReadRawFrame(jsServer.serverStdinRead);

    // Default (empty) serverKey must resolve to "html" -- with three
    // simultaneous bufferState_ entries (html, prose, javascript), the old
    // "whichever entry isn't kProseLanguageKey" scan could just as easily
    // have picked "javascript" first, silently sending a hover request at
    // the wrong server.
    manager.RequestHover(buffer, 0, [](std::optional<std::string>) {});
    const std::string raw = ReadRawFrame(htmlServer.serverStdinRead);
    REQUIRE(raw.find("textDocument/hover") != std::string::npos);
    REQUIRE(NoFrameArrives(proseServer.serverStdinRead));
    REQUIRE(NoFrameArrives(jsServer.serverStdinRead));
}

TEST_CASE("HandlePublishDiagnostics drops an embedded server's diagnostic whose start falls outside every owned range",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-diag-filter-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<div></div><script>let x = 1;</script>");

    LspClient* jsClient = nullptr;
    FakeServer jsServer = FakeServer::Create(manager, "javascript", eventLoop, jsClient);
    // Owned range covers only the "let x = 1;" content (offsets 20..30 in
    // the buffer above); everything else is padding as far as javascript is
    // concerned.
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = buffer.Text(), .ownedRanges = {{20, 30}}}});
    (void)ReadRawFrame(jsServer.serverStdinRead);

    const Json diagnosticsParams = {
        {"uri", "file://" + path.string()},
        {"diagnostics",
         Json::array({
             // Inside the owned range -- kept.
             {{"range", {{"start", {{"line", 0}, {"character", 20}}}, {"end", {{"line", 0}, {"character", 23}}}}},
              {"severity", 1},
              {"message", "kept: inside owned range"}},
             // Outside the owned range (in the padded <div></div> prefix) -- dropped.
             {{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 5}}}}},
              {"severity", 1},
              {"message", "dropped: outside owned range"}},
         })},
    };
    jsClient->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", diagnosticsParams}}.dump());
    WaitForDiagnosticCount(eventLoop, buffer, 1);

    REQUIRE(buffer.Diagnostics()[0].message == "kept: inside owned range");
}

TEST_CASE("LspManager::RequestHover with an explicit serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-serverkey-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    (void)ReadRawFrame(jsServer.serverStdinRead);

    bool                       invoked = false;
    std::optional<std::string> gotText;
    manager.RequestHover(
        buffer, 0, [&](std::optional<std::string> text) {
            invoked = true;
            gotText = std::move(text);
        },
        "javascript");

    REQUIRE(NoFrameArrives(htmlServer.serverStdinRead)); // never asked html

    const std::string raw = ReadRawFrame(jsServer.serverStdinRead);
    REQUIRE(raw.find("textDocument/hover") != std::string::npos);
    const Json request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result", {{"contents", "let x: number"}}},
    };
    jsClient->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotText == std::optional<std::string>("let x: number"));
}

TEST_CASE("LspManager::RequestDefinition with an explicit serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-serverkey-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>call_site();</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = "call_site();", .ownedRanges = {{0, 12}}}});
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    (void)ReadRawFrame(jsServer.serverStdinRead);

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestDefinition(
        buffer, 0,
        [&](std::vector<LspManager::ResolvedLocation> locations) {
            invoked = true;
            got     = std::move(locations);
        },
        "javascript");

    REQUIRE(NoFrameArrives(htmlServer.serverStdinRead));

    const std::string raw = ReadRawFrame(jsServer.serverStdinRead);
    REQUIRE(raw.find("textDocument/definition") != std::string::npos);
    const Json request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));

    const std::filesystem::path definitionPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-serverkey-target.js";
    const Json                  response       = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result", Json::array({{{"uri", "file://" + definitionPath.string()},
                                 {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 4}}}}}}})},
    };
    jsClient->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == definitionPath);
}

// LSP-deliberate-cuts follow-up: LspBackgroundSyncEnabled is process-wide
// state (see LspBackgroundSync.h) -- every test that flips it must leave it
// default-on for the next test, AutoRevertTest.cpp's own RAII-guard pattern.
namespace {
struct LspBackgroundSyncGuard {
    ~LspBackgroundSyncGuard() {
        ned::editor::lsp::SetLspBackgroundSyncEnabled(true);
    }
};
} // namespace

TEST_CASE("SyncBackgroundBuffers syncs every open, path-backed buffer, not just one", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    const std::filesystem::path cPath  = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-test.c";
    const std::filesystem::path pyPath = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-test.py";
    Buffer&                     cBuffer  = bufferList.OpenOrCreateFile(cPath);
    Buffer&                     pyBuffer = bufferList.OpenOrCreateFile(pyPath);

    LspClient* cClient  = nullptr;
    LspClient* pyClient = nullptr;
    FakeServer cServer  = FakeServer::Create(manager, "c", eventLoop, cClient);
    FakeServer pyServer = FakeServer::Create(manager, "python", eventLoop, pyClient);

    ned::editor::lsp::SyncBackgroundBuffers(bufferList, manager);

    const std::string cRaw  = ReadRawFrame(cServer.serverStdinRead);
    const Json        cOpen = Json::parse(cRaw.substr(cRaw.find("\r\n\r\n") + 4));
    REQUIRE(cOpen["method"] == "textDocument/didOpen");
    REQUIRE(cOpen["params"]["textDocument"]["languageId"] == "c");

    const std::string pyRaw  = ReadRawFrame(pyServer.serverStdinRead);
    const Json        pyOpen = Json::parse(pyRaw.substr(pyRaw.find("\r\n\r\n") + 4));
    REQUIRE(pyOpen["method"] == "textDocument/didOpen");
    REQUIRE(pyOpen["params"]["textDocument"]["languageId"] == "python");
}

TEST_CASE("SyncBackgroundBuffers skips a buffer with no path and a buffer still loading", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    Buffer& scratch = bufferList.CreateBuffer("scratch"); // no path
    (void)scratch;

    const std::filesystem::path loadingPath = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-loading-test.c";
    Buffer&                     loading     = bufferList.OpenOrCreateFile(loadingPath);
    loading.MarkLoading();

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "c", eventLoop, client);

    ned::editor::lsp::SyncBackgroundBuffers(bufferList, manager); // must not crash and must not sync either buffer

    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("SyncBackgroundBuffers is a no-op entirely when disabled", "[Lsp]") {
    LspBackgroundSyncGuard guard;
    BufferList              bufferList;
    ned::ui::EventLoop      eventLoop;
    LspManager              manager(bufferList, eventLoop);

    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-disabled-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "c", eventLoop, client);

    ned::editor::lsp::SetLspBackgroundSyncEnabled(false);
    ned::editor::lsp::SyncBackgroundBuffers(bufferList, manager);

    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

// graceful-lsp-shutdown follow-up.
TEST_CASE("LspManager::Shutdown sends shutdown then exit to a directly-spawned client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client); // brokerBacked defaults to false

    manager.Shutdown();

    const std::vector<Json> frames = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0]["method"] == "shutdown");
    REQUIRE(frames[1]["method"] == "exit");
}

TEST_CASE("LspManager::Shutdown never sends anything to a broker-backed client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client, Json::object(), /*brokerBacked=*/true);

    manager.Shutdown();

    // A broker-owned server is shared with other ned processes and the
    // broker daemon itself -- it must keep running after this process
    // exits, so it must never receive this process's own shutdown/exit.
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}
