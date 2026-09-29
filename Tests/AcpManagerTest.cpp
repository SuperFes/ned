#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/Acp/Client.h"
#include "Editor/Acp/Manager.h"
#include "Editor/Acp/Transport.h"
#include "Editor/Dap/Manager.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Mcp/BridgeServer.h"
#include "Editor/Mcp/ToolRegistry.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/Runner.h"
#include "Editor/WrapOverrides.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::acp::Client;
using ned::editor::acp::Manager;
using ned::editor::acp::Json;
using ned::editor::acp::Transport;

namespace {

// Newline-delimited equivalent of DapManagerTest's own buffered FrameReader
// -- a real handshake round-trip can write two messages back to back
// (initialize's response arriving triggers session/new immediately), so
// leftover bytes must survive between calls.
struct MessageReader {
    int         fd;
    std::string buffer;

    Json Next() {
        for (int i = 0; i < 16; ++i) {
            const auto newlinePos = buffer.find('\n');
            if (newlinePos != std::string::npos) {
                const std::string line = buffer.substr(0, newlinePos);
                buffer.erase(0, newlinePos + 1);
                return Json::parse(line);
            }
            char          chunk[512];
            const ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
        FAIL("no complete message available on fd");
        return Json::object();
    }
};

std::string ResultFrame(const Json& id, const Json& result) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump();
}

// An Manager plus a pipe-backed injected Client the test drives
// directly -- mirrors DapManagerTest's ManagerFixture/InjectClient exactly,
// adapted for ACP's newline framing and (agentName, not language) key.
struct ManagerFixture {
    ned::ui::EventLoop    eventLoop;
    ned::text::BufferList bufferList;
    Manager            manager{bufferList, eventLoop};
    int                   agentStdinRead   = -1;
    int                   agentStdoutWrite = -1;
    Client*            client           = nullptr;
    MessageReader         reader{-1};
    ned::text::Buffer*    outputBuffer = nullptr;

    void InjectClient() {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        agentStdinRead   = clientWritesHere[0];
        agentStdoutWrite = clientReadsHere[1];
        reader.fd        = agentStdinRead;
        client           = &manager.SetClientForTesting(
            std::make_unique<Client>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop));
    }

    // Runs StartSession through the initialize/session-new handshake against
    // the fake agent, leaving the session Active.
    void StartActiveSession(const std::string& agentName) {
        outputBuffer = manager.StartSession(agentName);
        REQUIRE(outputBuffer != nullptr);

        const Json initializeRequest = reader.Next();
        REQUIRE(initializeRequest["method"] == "initialize");
        client->DispatchFrame(ResultFrame(initializeRequest["id"], initializeResult));

        const Json sessionNewRequest = reader.Next();
        REQUIRE(sessionNewRequest["method"] == "session/new");
        Json newResult         = sessionNewResult;
        newResult["sessionId"] = "s1";
        client->DispatchFrame(ResultFrame(sessionNewRequest["id"], newResult));

        REQUIRE(manager.State() == Manager::SessionState::Active);
    }

    void SendUpdate(const Json& update) {
        client->DispatchFrame(
            Json{{"jsonrpc", "2.0"}, {"method", "session/update"}, {"params", {{"sessionId", "s1"}, {"update", update}}}}.dump());
    }

    // What StartActiveSession answers initialize and session/new with.
    Json initializeResult = Json::object();
    Json sessionNewResult = Json::object();

    ~ManagerFixture() {
        if (agentStdoutWrite >= 0) {
            ::close(agentStdoutWrite);
        }
        if (agentStdinRead >= 0) {
            ::close(agentStdinRead);
        }
    }
};

} // namespace

TEST_CASE("Manager::StartSession reports a clear error when nothing is configured for the agent name", "[Acp]") {
    ManagerFixture     fixture;
    ned::text::Buffer* buffer = fixture.manager.StartSession("an-agent-nobody-configured");
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->Text().find("No command configured") != std::string::npos);
    REQUIRE(fixture.manager.State() == Manager::SessionState::Inactive);
}

TEST_CASE("Manager::StartSession runs initialize then session/new and reaches Active", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE(fixture.outputBuffer->Text().find("[session ready]") != std::string::npos);
}

// ACP MCP tool-server bridge, slice 1.
TEST_CASE("Manager::StartSession advertises a stdio MCP server when a bridge is wired", "[Acp][Mcp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    ned::text::BufferList             mcpBufferList;
    ned::editor::lsp::Manager      lspManager(mcpBufferList, fixture.eventLoop);
    ned::editor::vcs::Runner       vcsRunner(fixture.eventLoop);
    ned::editor::testrun::TestRunner  testRunner(mcpBufferList, fixture.eventLoop);
    ned::editor::dap::Manager      dapManager(fixture.eventLoop);
    ned::editor::mcp::ToolRegistry    registry(mcpBufferList, lspManager, vcsRunner, testRunner, dapManager);
    ned::editor::mcp::BridgeServer bridge(registry, fixture.eventLoop);
    fixture.manager.SetMcpBridgeServer(&bridge);

    fixture.outputBuffer = fixture.manager.StartSession("test-agent");
    REQUIRE(fixture.outputBuffer != nullptr);

    const Json initializeRequest = fixture.reader.Next();
    REQUIRE(initializeRequest["method"] == "initialize");
    fixture.client->DispatchFrame(ResultFrame(initializeRequest["id"], Json::object()));

    const Json sessionNewRequest = fixture.reader.Next();
    REQUIRE(sessionNewRequest["method"] == "session/new");
    const Json& mcpServers = sessionNewRequest["params"]["mcpServers"];
    REQUIRE(mcpServers.size() == 1);
    REQUIRE(mcpServers[0]["type"] == "stdio");
    REQUIRE(mcpServers[0]["name"] == "ned");
    REQUIRE(mcpServers[0]["args"][0] == "--mcp-stdio-relay");
    REQUIRE(mcpServers[0]["args"][1] == bridge.SocketPath().string());
    REQUIRE(bridge.IsListening());

    fixture.client->DispatchFrame(ResultFrame(sessionNewRequest["id"], Json{{"sessionId", "s1"}}));
    REQUIRE(fixture.manager.State() == Manager::SessionState::Active);
}

TEST_CASE("Manager::SendPrompt with no active session reports that instead of sending anything", "[Acp]") {
    ManagerFixture fixture;
    REQUIRE(fixture.manager.SendPrompt("hello") == "No active ACP session (see acp-start-session).");
}

TEST_CASE("Manager::SendPrompt sends session/prompt and streams the stop reason back into the output buffer", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE(fixture.manager.SendPrompt("what does this do?") == "Sent.");

    const Json promptRequest = fixture.reader.Next();
    REQUIRE(promptRequest["method"] == "session/prompt");
    REQUIRE(promptRequest["params"]["sessionId"] == "s1");
    REQUIRE(promptRequest["params"]["prompt"][0]["text"] == "what does this do?");

    fixture.client->DispatchFrame(ResultFrame(promptRequest["id"], Json{{"stopReason", "end_turn"}}));

    REQUIRE(fixture.outputBuffer->Text().find("> what does this do?") != std::string::npos);
    REQUIRE(fixture.outputBuffer->Text().find("[end_turn]") != std::string::npos);
}

// ACP context auto-attach follow-up.
TEST_CASE("Manager::SendPrompt folds an attachment into the text block when the agent hasn't declared embeddedContext support",
          "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent"); // initialize result is Json::object() -- no promptCapabilities at all

    const Manager::PromptAttachment attachment{.uri = "file:///tmp/foo.cpp", .name = "foo.cpp", .mimeType = "", .text = "int main() {}"};
    REQUIRE(fixture.manager.SendPrompt("what does this do?", {attachment}) == "Sent.");

    const Json promptRequest = fixture.reader.Next();
    REQUIRE(promptRequest["params"]["prompt"].size() == 1);
    const std::string text = promptRequest["params"]["prompt"][0]["text"].get<std::string>();
    REQUIRE(text.find("what does this do?") != std::string::npos);
    REQUIRE(text.find("foo.cpp") != std::string::npos);
    REQUIRE(text.find("int main() {}") != std::string::npos);

    fixture.client->DispatchFrame(ResultFrame(promptRequest["id"], Json{{"stopReason", "end_turn"}}));
    // The transcript/output buffer shows a compact marker, not the folded-in content.
    REQUIRE(fixture.outputBuffer->Text().find("[attached: foo.cpp]") != std::string::npos);
}

TEST_CASE("Manager::SendPrompt sends a real resource content block when the agent declares embeddedContext support", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    fixture.outputBuffer = fixture.manager.StartSession("test-agent");
    REQUIRE(fixture.outputBuffer != nullptr);

    const Json initializeRequest = fixture.reader.Next();
    REQUIRE(initializeRequest["method"] == "initialize");
    fixture.client->DispatchFrame(
        ResultFrame(initializeRequest["id"], Json{{"agentCapabilities", {{"promptCapabilities", {{"embeddedContext", true}}}}}}));

    const Json sessionNewRequest = fixture.reader.Next();
    REQUIRE(sessionNewRequest["method"] == "session/new");
    fixture.client->DispatchFrame(ResultFrame(sessionNewRequest["id"], Json{{"sessionId", "s1"}}));
    REQUIRE(fixture.manager.State() == Manager::SessionState::Active);

    const Manager::PromptAttachment attachment{
        .uri = "file:///tmp/foo.cpp", .name = "foo.cpp", .mimeType = "text/x-c++", .text = "int main() {}"};
    REQUIRE(fixture.manager.SendPrompt("what does this do?", {attachment}) == "Sent.");

    const Json promptRequest = fixture.reader.Next();
    REQUIRE(promptRequest["params"]["prompt"].size() == 2);
    REQUIRE(promptRequest["params"]["prompt"][0]["text"] == "what does this do?");
    REQUIRE(promptRequest["params"]["prompt"][1]["type"] == "resource");
    REQUIRE(promptRequest["params"]["prompt"][1]["resource"]["uri"] == "file:///tmp/foo.cpp");
    REQUIRE(promptRequest["params"]["prompt"][1]["resource"]["text"] == "int main() {}");
    REQUIRE(promptRequest["params"]["prompt"][1]["resource"]["mimeType"] == "text/x-c++");
}

TEST_CASE("Manager's output buffer opts into word-wrap despite having no on-disk path", "[Acp]") {
    // acp-panel-wrapping follow-up: the "*acp: <agent>*" buffer is pure
    // in-memory (never backed by a real file), so ModeForBuffer always
    // resolves FundamentalMode()/wrapLines=false for it unless something
    // registers a buffer-Name()-keyed override -- this is that
    // registration, done once at buffer creation (see OutputBuffer).
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE(fixture.outputBuffer->Path() == std::nullopt);
    REQUIRE(ned::editor::WrapLinesForBufferNameOverride(fixture.outputBuffer->Name()) == std::optional<bool>(true));
}

TEST_CASE("Manager::PromptInFlight is true only while a session/prompt is outstanding", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE_FALSE(fixture.manager.PromptInFlight());

    REQUIRE(fixture.manager.SendPrompt("what does this do?") == "Sent.");
    REQUIRE(fixture.manager.PromptInFlight());

    const Json promptRequest = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(promptRequest["id"], Json{{"stopReason", "end_turn"}}));

    REQUIRE_FALSE(fixture.manager.PromptInFlight());
}

TEST_CASE("Manager::CancelPrompt sends session/cancel while a prompt is in flight, no-ops otherwise", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE_FALSE(fixture.manager.CancelPrompt()); // nothing in flight yet

    REQUIRE(fixture.manager.SendPrompt("keep going") == "Sent.");
    const Json promptRequest = fixture.reader.Next();

    REQUIRE(fixture.manager.CancelPrompt());
    const Json cancelNotification = fixture.reader.Next();
    REQUIRE(cancelNotification["method"] == "session/cancel");
    REQUIRE(cancelNotification["params"]["sessionId"] == "s1");
    REQUIRE_FALSE(cancelNotification.contains("id")); // a notification, not a request

    // The agent is expected to resolve the still-outstanding session/prompt
    // itself, typically with stopReason "cancelled" -- CancelPrompt doesn't
    // fabricate that locally.
    REQUIRE(fixture.manager.PromptInFlight());
    fixture.client->DispatchFrame(ResultFrame(promptRequest["id"], Json{{"stopReason", "cancelled"}}));
    REQUIRE_FALSE(fixture.manager.PromptInFlight());
    REQUIRE(fixture.manager.Transcript().back().kind == Manager::TranscriptEntry::Kind::SessionEvent);
    REQUIRE(fixture.manager.Transcript().back().text == "cancelled");
}

TEST_CASE("Manager streams an agent_message_chunk session/update into the output buffer", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    const Json update = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update", {{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", "Hello there"}}}}}}},
    };
    fixture.client->DispatchFrame(update.dump());

    REQUIRE(fixture.outputBuffer->Text().find("Hello there") != std::string::npos);
}

TEST_CASE("Manager answers fs/read_text_file from an open buffer's live content, not disk", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "ned-acp-manager-test-read.txt";
    {
        std::ofstream out(tempPath);
        out << "disk content";
    }
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(tempPath);
    buffer.InsertAtPoint("live edit -- "); // unsaved change, not reflected on disk

    fixture.StartActiveSession("test-agent");

    // Directly dispatch a fabricated agent-initiated fs/read_text_file
    // request the way the real agent process would.
    const Json request = {{"jsonrpc", "2.0"}, {"id", 99}, {"method", "fs/read_text_file"}, {"params", {{"path", tempPath.string()}}}};
    fixture.client->DispatchFrame(request.dump());
    const Json response = fixture.reader.Next();
    REQUIRE(response["id"] == 99);
    REQUIRE(response["result"]["content"].get<std::string>().find("live edit --") != std::string::npos);

    std::filesystem::remove(tempPath);
}

TEST_CASE("Manager's fs/write_text_file writes to disk and merges into an open, unmodified buffer via Revert", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "ned-acp-manager-test-write.txt";
    {
        std::ofstream out(tempPath);
        out << "original content";
    }
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(tempPath);
    REQUIRE_FALSE(buffer.Modified());

    fixture.StartActiveSession("test-agent");

    const Json request = {{"jsonrpc", "2.0"},
                          {"id", 5},
                          {"method", "fs/write_text_file"},
                          {"params", {{"path", tempPath.string()}, {"content", "agent-written content"}}}};
    fixture.client->DispatchFrame(request.dump());
    const Json response = fixture.reader.Next();
    REQUIRE(response["id"] == 5);
    REQUIRE(response.contains("result"));

    REQUIRE(buffer.Text() == "agent-written content");
    REQUIRE_FALSE(buffer.Modified());

    std::filesystem::remove(tempPath);
}

TEST_CASE("Manager's fs/write_text_file preserves the written file's permissions", "[Acp]") {
    // file-attribute-preservation follow-up: an agent writing on the user's
    // behalf is the last place a save should quietly drop a mode bit --
    // same temp-then-rename defect Buffer::SaveToFile had, see
    // Text/FilePreservation.h.
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "ned-acp-manager-test-write-perms.sh";
    {
        std::ofstream out(tempPath);
        out << "echo original";
    }
    std::filesystem::permissions(tempPath, std::filesystem::perms::owner_all);

    fixture.StartActiveSession("test-agent");

    const Json request = {{"jsonrpc", "2.0"},
                          {"id", 9},
                          {"method", "fs/write_text_file"},
                          {"params", {{"path", tempPath.string()}, {"content", "echo agent"}}}};
    fixture.client->DispatchFrame(request.dump());
    const Json response = fixture.reader.Next();
    REQUIRE(response["id"] == 9);
    REQUIRE(response.contains("result"));

    REQUIRE((std::filesystem::status(tempPath).permissions() & std::filesystem::perms::owner_exec) !=
            std::filesystem::perms::none);

    std::filesystem::remove(tempPath);
}

TEST_CASE("Manager routes session/request_permission to the registered handler and answers a selected option", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    Manager::PermissionPrompt captured;
    bool                         handlerCalled = false;
    fixture.manager.SetOnPermissionRequest([&](const Manager::PermissionPrompt& prompt) {
        handlerCalled = true;
        captured      = prompt;
    });

    const Json request = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/request_permission"},
        {"params",
         {{"sessionId", "s1"},
          {"toolCall", {{"title", "Edit main.cpp"}}},
          {"options", Json::array({Json{{"optionId", "allow-once"}, {"name", "Allow once"}, {"kind", "allow_once"}},
                                   Json{{"optionId", "reject-once"}, {"name", "Reject"}, {"kind", "reject_once"}}})}}},
    };
    fixture.client->DispatchFrame(request.dump());

    REQUIRE(handlerCalled);
    REQUIRE(captured.description == "Edit main.cpp");
    REQUIRE(captured.options.size() == 2);
    REQUIRE(fixture.manager.PendingPermissionPrompt().has_value());

    fixture.manager.ResolvePermissionPrompt("allow-once");

    const Json response = fixture.reader.Next();
    REQUIRE(response["id"] == 3);
    REQUIRE(response["result"]["outcome"]["outcome"] == "selected");
    REQUIRE(response["result"]["outcome"]["optionId"] == "allow-once");
    REQUIRE_FALSE(fixture.manager.PendingPermissionPrompt().has_value());
}

// diff-preview-line-diff-utility follow-up (ROADMAP "Diff preview before an
// agent edit's permission grant").
TEST_CASE("Manager parses a permission request's own diff content, when the toolCall carries one", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    const Json request = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/request_permission"},
        {"params",
         {{"sessionId", "s1"},
          {"toolCall",
           {{"title", "Edit main.cpp"},
            {"content", Json::array({Json{{"type", "diff"}, {"path", "main.cpp"}, {"oldText", "a\n"}, {"newText", "b\n"}}})}}},
          {"options", Json::array({Json{{"optionId", "allow-once"}, {"name", "Allow once"}, {"kind", "allow_once"}}})}}},
    };
    fixture.client->DispatchFrame(request.dump());

    REQUIRE(fixture.manager.PendingPermissionPrompt().has_value());
    REQUIRE(fixture.manager.PendingPermissionPrompt()->diffOldText == "a\n");
    REQUIRE(fixture.manager.PendingPermissionPrompt()->diffNewText == "b\n");
}

TEST_CASE("Manager leaves a permission request's diff fields unset when the toolCall carries no diff content", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    const Json request = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/request_permission"},
        {"params",
         {{"sessionId", "s1"},
          {"toolCall", {{"title", "Run a shell command"}}},
          {"options", Json::array({Json{{"optionId", "allow-once"}, {"name", "Allow once"}, {"kind", "allow_once"}}})}}},
    };
    fixture.client->DispatchFrame(request.dump());

    REQUIRE(fixture.manager.PendingPermissionPrompt().has_value());
    REQUIRE_FALSE(fixture.manager.PendingPermissionPrompt()->diffOldText.has_value());
    REQUIRE_FALSE(fixture.manager.PendingPermissionPrompt()->diffNewText.has_value());
}

TEST_CASE("Manager::StopSession tears the session down even with no active session", "[Acp]") {
    ManagerFixture fixture;
    REQUIRE(fixture.manager.StopSession() == "No active ACP session.");
}

TEST_CASE("Manager::StopSession sends session/close and reaches Inactive", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    // lsp-use-after-free follow-up: StopSession -> EndSession now destroys
    // the Client directly (immediate destruction is safe now that
    // Client itself guards against a stray Post()ed callback -- see
    // Client.h's own header comment on alive_), which joins its
    // background read thread as part of destruction. That thread is
    // deliberately still blocked in a real blocking read on this fixture's
    // fake pipe (nothing ever sent it real EOF) -- fine for the rest of this
    // fixture's life since nothing destroys the client early, but it would
    // hang forever here otherwise. Closing the fixture's own write end first
    // gives it real EOF, matching a real agent process actually exiting.
    ::close(fixture.agentStdoutWrite);
    fixture.agentStdoutWrite = -1; // fixture's own destructor must not double-close

    REQUIRE(fixture.manager.StopSession() == "ACP session stopped.");
    REQUIRE(fixture.manager.State() == Manager::SessionState::Inactive);

    const Json closeRequest = fixture.reader.Next();
    REQUIRE(closeRequest["method"] == "session/close");
}

namespace {

Json AgentMessageChunkUpdate(const std::string& text) {
    return Json{
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params", {{"sessionId", "s1"}, {"update", {{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", text}}}}}}},
    };
}

// ACP chat-feel round 2.
Json AgentThoughtChunkUpdate(const std::string& text) {
    return Json{
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params", {{"sessionId", "s1"}, {"update", {{"sessionUpdate", "agent_thought_chunk"}, {"content", {{"type", "text"}, {"text", text}}}}}}},
    };
}

} // namespace

TEST_CASE("Manager routes agent_thought_chunk into its own AgentThought entry, separate from AgentText", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    const std::size_t baseline = fixture.manager.Transcript().size();

    fixture.client->DispatchFrame(AgentThoughtChunkUpdate("considering the question").dump());
    fixture.client->DispatchFrame(AgentMessageChunkUpdate("the answer is 42").dump());

    const auto& transcript = fixture.manager.Transcript();
    REQUIRE(transcript.size() == baseline + 2);
    REQUIRE(transcript[baseline].kind == Manager::TranscriptEntry::Kind::AgentThought);
    REQUIRE(transcript[baseline].text == "considering the question");
    REQUIRE(transcript[baseline + 1].kind == Manager::TranscriptEntry::Kind::AgentText);
    REQUIRE(transcript[baseline + 1].text == "the answer is 42");
}

TEST_CASE("Manager coalesces consecutive agent_thought_chunk updates the same way agent_message_chunk does", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    const std::size_t baseline = fixture.manager.Transcript().size();

    fixture.client->DispatchFrame(AgentThoughtChunkUpdate("first").dump());
    fixture.client->DispatchFrame(AgentThoughtChunkUpdate(" second").dump());

    const auto& transcript = fixture.manager.Transcript();
    REQUIRE(transcript.size() == baseline + 1);
    REQUIRE(transcript.back().kind == Manager::TranscriptEntry::Kind::AgentThought);
    REQUIRE(transcript.back().text == "first second");
}

TEST_CASE("Manager coalesces consecutive agent_message_chunk updates into one transcript entry", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent"); // a successful start pushes no transcript event -- see StartSession
    const std::size_t baseline             = fixture.manager.Transcript().size();
    const std::size_t generationAfterStart = fixture.manager.TranscriptGeneration();

    fixture.client->DispatchFrame(AgentMessageChunkUpdate("Hello").dump());
    fixture.client->DispatchFrame(AgentMessageChunkUpdate(" there").dump());

    const auto& transcript = fixture.manager.Transcript();
    REQUIRE(transcript.size() == baseline + 1);
    REQUIRE(transcript.back().kind == Manager::TranscriptEntry::Kind::AgentText);
    REQUIRE(transcript.back().text == "Hello there");
    REQUIRE(fixture.manager.TranscriptGeneration() == generationAfterStart + 2);
}

TEST_CASE("Manager's transcript entry count and text mirror a session/prompt exchange", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.manager.SendPrompt("what does this do?");
    const Json promptRequest = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(promptRequest["id"], Json{{"stopReason", "end_turn"}}));

    const auto& transcript = fixture.manager.Transcript();
    const auto  userEntry =
        std::find_if(transcript.begin(), transcript.end(), [](const auto& e) { return e.kind == Manager::TranscriptEntry::Kind::UserMessage; });
    REQUIRE(userEntry != transcript.end());
    REQUIRE(userEntry->text == "what does this do?");
}

TEST_CASE("Manager parses a plan session/update into one Plan transcript entry and replaces it on the next plan update", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    const std::size_t baseline = fixture.manager.Transcript().size();

    const Json firstPlan = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update",
           {{"sessionUpdate", "plan"},
            {"entries", Json::array({Json{{"content", "Trim common suffix first"}, {"status", "completed"}},
                                     Json{{"content", "Re-run LCS diff"}, {"status", "pending"}}})}}}}},
    };
    fixture.client->DispatchFrame(firstPlan.dump());

    REQUIRE(fixture.manager.Transcript().size() == baseline + 1);
    const auto& planEntry = fixture.manager.Transcript().back();
    REQUIRE(planEntry.kind == Manager::TranscriptEntry::Kind::Plan);
    REQUIRE(planEntry.planSteps.size() == 2);
    REQUIRE(planEntry.planSteps[0] == "[x] Trim common suffix first");
    REQUIRE(planEntry.planSteps[1] == "[ ] Re-run LCS diff");

    const std::size_t generationAfterFirstPlan = fixture.manager.TranscriptGeneration();

    const Json secondPlan = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update",
           {{"sessionUpdate", "plan"},
            {"entries", Json::array({Json{{"content", "Trim common suffix first"}, {"status", "completed"}},
                                     Json{{"content", "Re-run LCS diff"}, {"status", "completed"}},
                                     Json{{"content", "Update tests"}, {"status", "pending"}}})}}}}},
    };
    fixture.client->DispatchFrame(secondPlan.dump());

    // Replaced in place, not appended -- same entry count, new content.
    REQUIRE(fixture.manager.Transcript().size() == baseline + 1);
    REQUIRE(fixture.manager.Transcript().back().planSteps.size() == 3);
    REQUIRE(fixture.manager.Transcript().back().planSteps[1] == "[x] Re-run LCS diff");
    REQUIRE(fixture.manager.TranscriptGeneration() > generationAfterFirstPlan);
}

TEST_CASE("Manager matches tool_call_update to its tool_call by toolCallId instead of appending a duplicate entry", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    const std::size_t baseline = fixture.manager.Transcript().size();

    const Json toolCall = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update", {{"sessionUpdate", "tool_call"}, {"toolCallId", "tc1"}, {"title", "read_text_file"}, {"status", "pending"}}}}},
    };
    fixture.client->DispatchFrame(toolCall.dump());

    REQUIRE(fixture.manager.Transcript().size() == baseline + 1);
    REQUIRE(fixture.manager.Transcript().back().status == "pending");

    const Json toolCallUpdate = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update", {{"sessionUpdate", "tool_call_update"}, {"toolCallId", "tc1"}, {"title", "read_text_file"}, {"status", "completed"}}}}},
    };
    fixture.client->DispatchFrame(toolCallUpdate.dump());

    REQUIRE(fixture.manager.Transcript().size() == baseline + 1); // updated in place, not appended
    REQUIRE(fixture.manager.Transcript().back().status == "completed");
}

TEST_CASE("Manager::SetOnTranscriptChanged fires on every transcript-affecting event", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    int callCount = 0;
    fixture.manager.SetOnTranscriptChanged([&] { ++callCount; });

    fixture.StartActiveSession("test-agent"); // a successful start pushes no transcript event -- see StartSession
    REQUIRE(callCount == 0);

    fixture.client->DispatchFrame(AgentMessageChunkUpdate("hi").dump());
    REQUIRE(callCount == 1);
}

// ACP checkpoint/rewind follow-up.
namespace {

// Dispatches an agent-initiated fs/write_text_file the way a real agent
// mid-turn tool call would, and drains its response -- request ids just
// need to be unique per test, requestId is the caller's own counter.
void WriteFileViaAgent(ManagerFixture& fixture, const std::filesystem::path& path, const std::string& content, int requestId) {
    const Json request = {{"jsonrpc", "2.0"},
                          {"id", requestId},
                          {"method", "fs/write_text_file"},
                          {"params", {{"path", path.string()}, {"content", content}}}};
    fixture.client->DispatchFrame(request.dump());
    (void)fixture.reader.Next();
}

// Runs one whole turn -- SendPrompt, an agent-initiated fs/write_text_file
// mid-turn (the write has to land *before* the session/prompt response,
// exactly like a real tool call would, so Manager's pendingCheckpoint_
// is still open to record it), then resolves session/prompt.
void RunTurnWithFileWrite(ManagerFixture& fixture, const std::string& promptText, const std::filesystem::path& path,
                          const std::string& content, int requestId, const std::string& stopReason = "end_turn") {
    REQUIRE(fixture.manager.SendPrompt(promptText) == "Sent.");
    const Json promptRequest = fixture.reader.Next();
    WriteFileViaAgent(fixture, path, content, requestId);
    fixture.client->DispatchFrame(ResultFrame(promptRequest["id"], Json{{"stopReason", stopReason}}));
}

} // namespace

TEST_CASE("Manager checkpoints a turn's file edit and RewindTo reverts it, truncating the transcript", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "ned-acp-manager-test-rewind-1.txt";
    {
        std::ofstream out(tempPath);
        out << "original content";
    }
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(tempPath);
    fixture.StartActiveSession("test-agent");

    RunTurnWithFileWrite(fixture, "please rewrite the file", tempPath, "agent-written content", 5);
    REQUIRE(buffer.Text() == "agent-written content");
    REQUIRE(fixture.manager.CheckpointCount() == 1);
    REQUIRE(fixture.manager.CheckpointAt(0).promptPreview == "please rewrite the file");
    REQUIRE(fixture.manager.CheckpointAt(0).fileRecords.size() == 1);

    const Manager::RewindOutcome outcome = fixture.manager.RewindTo(0);
    REQUIRE(outcome.turnsRewound == 1);
    REQUIRE(outcome.revertedFiles == std::vector<std::string>{tempPath.string()});
    REQUIRE(outcome.divergedFiles.empty());
    REQUIRE(outcome.untrackedFiles.empty());
    REQUIRE(buffer.Text() == "original content");
    REQUIRE(fixture.manager.CheckpointCount() == 0);

    // The turn's own UserMessage entry is gone, replaced by exactly one
    // SessionEvent summarizing the rewind.
    REQUIRE(fixture.manager.Transcript().size() == 1);
    REQUIRE(fixture.manager.Transcript().back().kind == Manager::TranscriptEntry::Kind::SessionEvent);
    REQUIRE(fixture.manager.Transcript().back().text.find("rewound") != std::string::npos);

    std::filesystem::remove(tempPath);
}

TEST_CASE("Manager::RewindTo on a later checkpoint leaves an earlier turn's edit and transcript entry intact", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "ned-acp-manager-test-rewind-2.txt";
    {
        std::ofstream out(tempPath);
        out << "v0";
    }
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(tempPath);
    fixture.StartActiveSession("test-agent");

    RunTurnWithFileWrite(fixture, "first turn", tempPath, "v1", 5);
    RunTurnWithFileWrite(fixture, "second turn", tempPath, "v2", 6);
    REQUIRE(buffer.Text() == "v2");
    REQUIRE(fixture.manager.CheckpointCount() == 2);

    // Rewind only the most recent checkpoint (index 1) -- the first turn's
    // own edit and UserMessage entry must survive untouched.
    const Manager::RewindOutcome outcome = fixture.manager.RewindTo(1);
    REQUIRE(outcome.turnsRewound == 1);
    REQUIRE(buffer.Text() == "v1");
    REQUIRE(fixture.manager.CheckpointCount() == 1);

    const auto& transcript     = fixture.manager.Transcript();
    const auto  firstTurnEntry = std::find_if(transcript.begin(), transcript.end(), [](const auto& e) {
        return e.kind == Manager::TranscriptEntry::Kind::UserMessage && e.text == "first turn";
    });
    REQUIRE(firstTurnEntry != transcript.end());
    const auto secondTurnEntry = std::find_if(transcript.begin(), transcript.end(), [](const auto& e) {
        return e.kind == Manager::TranscriptEntry::Kind::UserMessage && e.text == "second turn";
    });
    REQUIRE(secondTurnEntry == transcript.end()); // truncated away

    std::filesystem::remove(tempPath);
}

TEST_CASE("Manager::RewindTo reports a file edited again since its turn as diverged, leaving it untouched", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "ned-acp-manager-test-rewind-diverge.txt";
    {
        std::ofstream out(tempPath);
        out << "original";
    }
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(tempPath);
    fixture.StartActiveSession("test-agent");

    RunTurnWithFileWrite(fixture, "rewrite it", tempPath, "agent version", 5);
    REQUIRE(fixture.manager.CheckpointCount() == 1);

    // A local edit after the turn, not tracked by any checkpoint -- the
    // buffer has now moved past this turn's own afterSequence.
    buffer.InsertAtPoint("!");
    const std::string editedText = buffer.Text();

    const Manager::RewindOutcome outcome = fixture.manager.RewindTo(0);
    REQUIRE(outcome.divergedFiles == std::vector<std::string>{tempPath.string()});
    REQUIRE(outcome.revertedFiles.empty());
    REQUIRE(buffer.Text() == editedText); // left alone, not force-reverted

    std::filesystem::remove(tempPath);
}

TEST_CASE("Manager::RewindTo reports a file never open in ned as untracked, without touching it on disk", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "ned-acp-manager-test-rewind-untracked.txt";
    {
        std::ofstream out(tempPath);
        out << "original";
    }
    // Deliberately never opened as a ned buffer.
    fixture.StartActiveSession("test-agent");

    RunTurnWithFileWrite(fixture, "rewrite an unopened file", tempPath, "agent version", 5);
    REQUIRE(fixture.manager.CheckpointCount() == 1);
    REQUIRE(fixture.manager.CheckpointAt(0).fileRecords.empty());
    REQUIRE(fixture.manager.CheckpointAt(0).untrackedPaths == std::vector<std::filesystem::path>{tempPath});

    const Manager::RewindOutcome outcome = fixture.manager.RewindTo(0);
    REQUIRE(outcome.untrackedFiles == std::vector<std::string>{tempPath.string()});
    REQUIRE(outcome.revertedFiles.empty());

    std::ifstream diskContent(tempPath);
    std::string   contentOnDisk((std::istreambuf_iterator<char>(diskContent)), std::istreambuf_iterator<char>());
    REQUIRE(contentOnDisk == "agent version"); // RewindTo never touches disk directly

    std::filesystem::remove(tempPath);
}

TEST_CASE("Manager::RewindTo with an out-of-range index is a no-op", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    const std::size_t               transcriptSize = fixture.manager.Transcript().size();
    const Manager::RewindOutcome outcome        = fixture.manager.RewindTo(0); // no checkpoints exist yet
    REQUIRE(outcome.turnsRewound == 0);
    REQUIRE(outcome.revertedFiles.empty());
    REQUIRE(fixture.manager.Transcript().size() == transcriptSize);
}

namespace {

// The shape claude-agent-acp 0.84 answers session/new with, trimmed.
Json ClaudeSessionSettings() {
    const Json modeChoices = Json::array({Json{{"value", "default"}, {"name", "Manual"}},
                                          Json{{"value", "acceptEdits"}, {"name", "Accept edits"}},
                                          Json{{"value", "plan"}, {"name", "Plan"}},
                                          Json{{"value", "bypassPermissions"}, {"name", "Bypass permissions"}}});
    return {{"modes",
             {{"currentModeId", "default"},
              {"availableModes", Json::array({Json{{"id", "default"}, {"name", "Manual"}},
                                              Json{{"id", "acceptEdits"}, {"name", "Accept edits"}},
                                              Json{{"id", "plan"}, {"name", "Plan"}},
                                              Json{{"id", "bypassPermissions"}, {"name", "Bypass permissions"}}})}}},
            {"configOptions",
             Json::array({Json{{"id", "mode"}, {"name", "Mode"}, {"category", "mode"}, {"type", "select"}, {"currentValue", "default"}, {"options", modeChoices}},
                          Json{{"id", "model"},
                               {"name", "Model"},
                               {"category", "model"},
                               {"type", "select"},
                               {"currentValue", "default"},
                               {"options",
                                Json::array({Json{{"value", "default"}, {"name", "Default (recommended)"}},
                                             Json{{"group", "Older"}, {"name", "Older"}, {"options", Json::array({Json{{"value", "haiku"}, {"name", "Haiku"}}})}}})}},
                          Json{{"id", "fast"}, {"name", "Fast mode"}, {"type", "boolean"}, {"currentValue", false}}})}};
}

} // namespace

TEST_CASE("Manager parses the modes and config options session/new advertises", "[Acp]") {
    ManagerFixture fixture;
    fixture.sessionNewResult = ClaudeSessionSettings();
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE(fixture.manager.Modes().size() == 4);
    REQUIRE(fixture.manager.CurrentModeId() == "default");
    REQUIRE(fixture.manager.ConfigOptions().size() == 3);
    const Manager::ConfigOption* model = fixture.manager.ConfigOptionByCategory("model");
    REQUIRE(model != nullptr);
    REQUIRE(model->choices.size() == 2); // the grouped "haiku" flattened in
    REQUIRE(model->choices[1].value == "haiku");
    REQUIRE(fixture.manager.ConfigOptions()[2].currentValue == "false");
}

TEST_CASE("Manager::CycleMode sets the next mode through the mode config option, skipping bypassPermissions", "[Acp]") {
    ManagerFixture fixture;
    fixture.sessionNewResult = ClaudeSessionSettings();
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.manager.CycleMode();
    Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "session/set_config_option");
    REQUIRE(request["params"]["configId"] == "mode");
    REQUIRE(request["params"]["value"] == "acceptEdits");
    Json options               = ClaudeSessionSettings()["configOptions"];
    options[0]["currentValue"] = "plan";
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"configOptions", options}}));
    REQUIRE(fixture.manager.CurrentModeId() == "plan");

    fixture.manager.CycleMode();
    request = fixture.reader.Next();
    REQUIRE(request["params"]["value"] == "default"); // wrapped, never "bypassPermissions"
}

TEST_CASE("Manager::SetMode falls back to session/set_mode without a mode config option", "[Acp]") {
    ManagerFixture fixture;
    fixture.sessionNewResult = {{"modes", ClaudeSessionSettings()["modes"]}};
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.manager.SetMode("plan");
    const Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "session/set_mode");
    REQUIRE(request["params"]["modeId"] == "plan");
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json::object()));
    REQUIRE(fixture.manager.CurrentModeId() == "plan");
}

TEST_CASE("Manager sends a boolean config option's value as a JSON boolean", "[Acp]") {
    ManagerFixture fixture;
    fixture.sessionNewResult = ClaudeSessionSettings();
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.manager.SetConfigOption("fast", "true");
    const Json request = fixture.reader.Next();
    REQUIRE(request["params"]["value"] == true);
}

TEST_CASE("Manager tracks agent-initiated mode changes, session titles and usage", "[Acp]") {
    ManagerFixture fixture;
    fixture.sessionNewResult = ClaudeSessionSettings();
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.SendUpdate({{"sessionUpdate", "current_mode_update"}, {"currentModeId", "plan"}});
    REQUIRE(fixture.manager.CurrentModeId() == "plan");
    REQUIRE(fixture.manager.ConfigOptionByCategory("mode")->currentValue == "plan");
    REQUIRE(fixture.manager.Transcript().back().text == "mode: Plan");

    fixture.SendUpdate({{"sessionUpdate", "session_info_update"}, {"title", "Fix the parser"}});
    REQUIRE(fixture.manager.SessionTitle() == "Fix the parser");

    fixture.SendUpdate({{"sessionUpdate", "usage_update"}, {"used", 50000}, {"size", 200000}, {"cost", {{"amount", 0.25}, {"currency", "USD"}}}});
    REQUIRE(fixture.manager.SessionUsage()->used == 50000);
    REQUIRE(fixture.manager.SessionUsage()->size == 200000);
    REQUIRE(fixture.manager.SessionUsage()->costAmount == 0.25);

    fixture.SendUpdate({{"sessionUpdate", "usage_update"}, {"used", "garbage"}});
    REQUIRE(fixture.manager.SessionUsage()->used == 0);
}

namespace {

Json ResumableAgent(bool load = true) {
    Json caps{{"sessionCapabilities", {{"list", Json::object()}, {"resume", Json::object()}}}};
    if (load) {
        caps["loadSession"] = true;
    }
    return {{"agentCapabilities", caps}};
}

void SendUpdateFor(ManagerFixture& fixture, const std::string& sessionId, const Json& update) {
    fixture.client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"method", "session/update"}, {"params", {{"sessionId", sessionId}, {"update", update}}}}.dump());
}

Json UserChunk(const std::string& text, const std::string& messageId) {
    return {{"sessionUpdate", "user_message_chunk"}, {"messageId", messageId}, {"content", {{"type", "text"}, {"text", text}}}};
}

} // namespace

TEST_CASE("Manager::ListSessions reports an agent that can't list sessions", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    std::string error;
    fixture.manager.ListSessions([&](std::vector<Manager::SessionSummary>, std::string e) { error = std::move(e); });
    REQUIRE(error == "This agent can't list or resume its sessions.");
}

TEST_CASE("Manager::ListSessions follows session/list's cursor across pages", "[Acp]") {
    ManagerFixture fixture;
    fixture.initializeResult = ResumableAgent();
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    std::vector<Manager::SessionSummary> sessions;
    bool                                 done = false;
    fixture.manager.ListSessions([&](std::vector<Manager::SessionSummary> s, std::string) {
        sessions = std::move(s);
        done     = true;
    });
    Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "session/list");
    REQUIRE(request["params"]["cwd"].is_string());
    REQUIRE_FALSE(request["params"].contains("cursor"));
    fixture.client->DispatchFrame(ResultFrame(
        request["id"], Json{{"sessions", Json::array({Json{{"sessionId", "a"}, {"title", "First"}, {"updatedAt", "2026-09-29T10:00:00Z"}}})},
                            {"nextCursor", "page2"}}));
    request = fixture.reader.Next();
    REQUIRE(request["params"]["cursor"] == "page2");
    REQUIRE_FALSE(done);
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"sessions", Json::array({Json{{"sessionId", "b"}}})}}));

    REQUIRE(done);
    REQUIRE(sessions.size() == 2);
    REQUIRE(sessions[0].title == "First");
    REQUIRE(sessions[1].sessionId == "b");
    REQUIRE(sessions[1].title.empty());
}

TEST_CASE("Manager::LoadSession replays the conversation, coalescing a prompt's chunks by messageId", "[Acp]") {
    ManagerFixture fixture;
    fixture.initializeResult = ResumableAgent();
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE(fixture.manager.LoadSession("s2", "Old work") == "Resuming Old work.");
    const Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "session/load");
    REQUIRE(request["params"]["sessionId"] == "s2");

    SendUpdateFor(fixture, "s1", {{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", "stray"}}}});
    SendUpdateFor(fixture, "s2", UserChunk("hello ", "m1"));
    SendUpdateFor(fixture, "s2", UserChunk("world", "m1"));
    SendUpdateFor(fixture, "s2", {{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", "hi"}}}});
    SendUpdateFor(fixture, "s2", UserChunk("again", "m2"));
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"modes", {{"currentModeId", "plan"}, {"availableModes", Json::array()}}}}));

    using Kind              = Manager::TranscriptEntry::Kind;
    const auto& transcript  = fixture.manager.Transcript();
    const auto  resumeEvent = std::find_if(transcript.begin(), transcript.end(),
                                           [](const auto& entry) { return entry.text == "resuming: Old work"; });
    REQUIRE(resumeEvent != transcript.end());
    REQUIRE(transcript.end() - resumeEvent == 4);
    REQUIRE((resumeEvent + 1)->kind == Kind::UserMessage);
    REQUIRE((resumeEvent + 1)->text == "hello world");
    REQUIRE((resumeEvent + 2)->text == "hi");
    REQUIRE((resumeEvent + 3)->text == "again");
    REQUIRE(fixture.manager.SessionId() == "s2");
    REQUIRE(fixture.manager.CurrentModeId() == "plan");

    // Live again: a user_message_chunk is once more an echo, not a prompt.
    SendUpdateFor(fixture, "s2", UserChunk("echo", "m3"));
    REQUIRE(transcript.back().text == "again");
}

TEST_CASE("Manager::LoadSession falls back to the previous session when the load fails", "[Acp]") {
    ManagerFixture fixture;
    fixture.initializeResult = ResumableAgent();
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.manager.LoadSession("gone", "");
    const Json request = fixture.reader.Next();
    fixture.client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", request["id"]}, {"error", {{"code", -32602}, {"message", "no such session"}}}}.dump());
    REQUIRE(fixture.manager.SessionId() == "s1");
    REQUIRE(fixture.manager.Transcript().back().text == "resume failed: no such session");
}

TEST_CASE("Manager::LoadSession uses session/resume when the agent can't replay", "[Acp]") {
    ManagerFixture fixture;
    fixture.initializeResult = ResumableAgent(false);
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.manager.LoadSession("s2", "Old work");
    const Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "session/resume");
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json::object()));
    REQUIRE(fixture.manager.Transcript().back().text == "history not replayed -- the agent only resumes");
}

TEST_CASE("Manager::ListSessions asked mid-handshake answers once the session is up", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.manager.StartSession("test-agent");
    bool answered = false;
    fixture.manager.ListSessions([&](std::vector<Manager::SessionSummary>, std::string) { answered = true; });
    REQUIRE_FALSE(answered);

    const Json initializeRequest = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(initializeRequest["id"], ResumableAgent()));
    const Json sessionNewRequest = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(sessionNewRequest["id"], Json{{"sessionId", "s1"}}));
    const Json listRequest = fixture.reader.Next();
    REQUIRE(listRequest["method"] == "session/list");
    fixture.client->DispatchFrame(ResultFrame(listRequest["id"], Json{{"sessions", Json::array()}}));
    REQUIRE(answered);
}

TEST_CASE("Manager sends queued prompts one per turn, in order, once each turn ends", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.manager.SendPrompt("first");
    Json request = fixture.reader.Next();
    fixture.manager.QueuePrompt({.text = "second"});
    fixture.manager.QueuePrompt({.text = "third"});
    REQUIRE(fixture.manager.QueuedPrompts().size() == 2);

    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"stopReason", "end_turn"}}));
    request = fixture.reader.Next();
    REQUIRE(request["params"]["prompt"][0]["text"] == "second");
    REQUIRE(fixture.manager.QueuedPrompts().size() == 1);
    REQUIRE(fixture.manager.PromptInFlight());
}

TEST_CASE("Manager holds the queue after a cancelled turn and drops it when the session ends", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    fixture.manager.SendPrompt("first");
    const Json request = fixture.reader.Next();
    fixture.manager.QueuePrompt({.text = "second"});
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"stopReason", "cancelled"}}));
    REQUIRE(fixture.manager.QueuedPrompts().size() == 1);
    REQUIRE_FALSE(fixture.manager.PromptInFlight());

    // EOF first, so teardown's reader thread can exit -- see the StopSession
    // test above.
    ::close(fixture.agentStdoutWrite);
    fixture.agentStdoutWrite = -1;
    fixture.manager.StopSession();
    REQUIRE(fixture.manager.QueuedPrompts().empty());
    const auto& transcript = fixture.manager.Transcript();
    REQUIRE(std::any_of(transcript.begin(), transcript.end(), [](const auto& entry) { return entry.text == "not sent: second"; }));
}

TEST_CASE("Manager::Steer queues when the agent can't steer", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    fixture.manager.SendPrompt("first");
    (void)fixture.reader.Next();

    REQUIRE(fixture.manager.Steer({.text = "also this"}).find("queued") != std::string::npos);
    REQUIRE(fixture.manager.QueuedPrompts().size() == 1);
}

TEST_CASE("Manager::Steer injects into the running turn through _session/steering", "[Acp]") {
    ManagerFixture fixture;
    fixture.initializeResult = {{"_meta", {{"steering", {{"supported", true}}}}}};
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    REQUIRE(fixture.manager.SupportsSteering());
    fixture.manager.SendPrompt("first");
    (void)fixture.reader.Next();

    REQUIRE(fixture.manager.Steer({.text = "use the other API"}) == "Steering.");
    const Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "_session/steering");
    REQUIRE(request["params"]["sessionId"] == "s1");
    REQUIRE(request["params"]["prompt"][0]["text"] == "use the other API");
    REQUIRE(request["params"]["_meta"]["steering"]["idleBehavior"] == "promptRequired");

    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"outcome", "injected"}}));
    REQUIRE(fixture.manager.Transcript().back().text == "use the other API");
    REQUIRE(fixture.manager.Transcript().back().status == "steered");
}

TEST_CASE("Manager::Steer sends a plain prompt once the turn it aimed at has ended", "[Acp]") {
    ManagerFixture fixture;
    fixture.initializeResult = {{"_meta", {{"steering", {{"supported", true}}}}}};
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    fixture.manager.SendPrompt("first");
    const Json prompt = fixture.reader.Next();

    fixture.manager.Steer({.text = "late"});
    const Json steering = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(prompt["id"], Json{{"stopReason", "end_turn"}}));
    fixture.client->DispatchFrame(ResultFrame(steering["id"], Json{{"outcome", "promptRequired"}, {"reason", "noRunningTurn"}}));

    const Json followUp = fixture.reader.Next();
    REQUIRE(followUp["method"] == "session/prompt");
    REQUIRE(followUp["params"]["prompt"][0]["text"] == "late");
}

TEST_CASE("Manager calls for attention when a turn finishes, but not between queued turns or after a cancel", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    std::vector<Manager::Attention> seen;
    fixture.manager.SetOnAttention([&seen](Manager::Attention attention, std::chrono::steady_clock::duration) { seen.push_back(attention); });

    fixture.manager.SendPrompt("first");
    Json request = fixture.reader.Next();
    fixture.manager.QueuePrompt({.text = "second"});
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"stopReason", "end_turn"}}));
    REQUIRE(seen.empty());

    request = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"stopReason", "end_turn"}}));
    REQUIRE(seen == std::vector<Manager::Attention>{Manager::Attention::TurnFinished});

    fixture.manager.SendPrompt("third");
    request = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"stopReason", "cancelled"}}));
    REQUIRE(seen.size() == 1);
}

TEST_CASE("Manager calls for attention when the agent asks for permission", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");
    std::vector<Manager::Attention> seen;
    fixture.manager.SetOnAttention([&seen](Manager::Attention attention, std::chrono::steady_clock::duration) { seen.push_back(attention); });

    fixture.client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                                       {"id", 50},
                                       {"method", "session/request_permission"},
                                       {"params",
                                        {{"sessionId", "s1"},
                                         {"toolCall", {{"toolCallId", "t1"}, {"title", "rm -rf build"}}},
                                         {"options", Json::array({Json{{"optionId", "y"}, {"name", "Allow"}, {"kind", "allow_once"}}})}}}}
                                      .dump());
    REQUIRE(seen == std::vector<Manager::Attention>{Manager::Attention::PermissionRequested});
}

TEST_CASE("Manager reads null or mistyped fields in agent updates as absent instead of throwing", "[Acp]") {
    ManagerFixture fixture;
    fixture.initializeResult = {{"agentCapabilities", {{"loadSession", nullptr}, {"promptCapabilities", {{"embeddedContext", "yes"}}}}}};
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE_NOTHROW(fixture.SendUpdate({{"sessionUpdate", "tool_call"}, {"toolCallId", "t1"}, {"title", nullptr}, {"kind", "read"}, {"status", nullptr}}));
    REQUIRE(fixture.manager.Transcript().back().text == "read");
    REQUIRE_NOTHROW(fixture.SendUpdate({{"sessionUpdate", "tool_call_update"}, {"toolCallId", "t1"}, {"title", nullptr}, {"status", "completed"}}));
    REQUIRE(fixture.manager.Transcript().back().text == "read");
    REQUIRE(fixture.manager.Transcript().back().status == "completed");
    REQUIRE_NOTHROW(fixture.SendUpdate({{"sessionUpdate", "agent_message_chunk"}, {"messageId", nullptr}, {"content", {{"type", "text"}, {"text", nullptr}}}}));
    REQUIRE_NOTHROW(fixture.SendUpdate({{"sessionUpdate", nullptr}}));
}
