#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/BackgroundActivity.h"
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

    static FakeServer Create(LspManager& manager, const std::string& language, ned::ui::EventLoop& eventLoop, LspClient*& outClient) {
        int clientWritesHere[2]; // client's write end -> test's read end
        int clientReadsHere[2];  // test's write end -> client's read end
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<LspClient>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient   = &manager.SetClientForTesting(language, std::move(client));
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

int RequestIdFromFrame(const std::string& raw) {
    return Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["id"].get<int>();
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
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-spawn-fail-test.txt");

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

    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].message == "syntax error");
    REQUIRE(buffer.Diagnostics()[0].severity == Buffer::Diagnostic::Severity::Error);
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

TEST_CASE("LspManager::RequestDefinition resolves a Location[] response's uris to real paths", "[Lsp]") {
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
