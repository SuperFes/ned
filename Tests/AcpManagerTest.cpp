#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/Acp/AcpClient.h"
#include "Editor/Acp/AcpManager.h"
#include "Editor/Acp/Transport.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::acp::AcpClient;
using ned::editor::acp::AcpManager;
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

// An AcpManager plus a pipe-backed injected AcpClient the test drives
// directly -- mirrors DapManagerTest's ManagerFixture/InjectClient exactly,
// adapted for ACP's newline framing and (agentName, not language) key.
struct ManagerFixture {
    ned::ui::EventLoop    eventLoop;
    ned::text::BufferList bufferList;
    AcpManager            manager{bufferList, eventLoop};
    int                   agentStdinRead   = -1;
    int                   agentStdoutWrite = -1;
    AcpClient*            client           = nullptr;
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
            std::make_unique<AcpClient>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop));
    }

    // Runs StartSession through the initialize/session-new handshake against
    // the fake agent, leaving the session Active.
    void StartActiveSession(const std::string& agentName) {
        outputBuffer = manager.StartSession(agentName);
        REQUIRE(outputBuffer != nullptr);

        const Json initializeRequest = reader.Next();
        REQUIRE(initializeRequest["method"] == "initialize");
        client->DispatchFrame(ResultFrame(initializeRequest["id"], Json::object()));

        const Json sessionNewRequest = reader.Next();
        REQUIRE(sessionNewRequest["method"] == "session/new");
        client->DispatchFrame(ResultFrame(sessionNewRequest["id"], Json{{"sessionId", "s1"}}));

        REQUIRE(manager.State() == AcpManager::SessionState::Active);
    }

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

TEST_CASE("AcpManager::StartSession reports a clear error when nothing is configured for the agent name", "[Acp]") {
    ManagerFixture     fixture;
    ned::text::Buffer* buffer = fixture.manager.StartSession("an-agent-nobody-configured");
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->Text().find("No command configured") != std::string::npos);
    REQUIRE(fixture.manager.State() == AcpManager::SessionState::Inactive);
}

TEST_CASE("AcpManager::StartSession runs initialize then session/new and reaches Active", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE(fixture.outputBuffer->Text().find("[session ready]") != std::string::npos);
}

TEST_CASE("AcpManager::SendPrompt with no active session reports that instead of sending anything", "[Acp]") {
    ManagerFixture fixture;
    REQUIRE(fixture.manager.SendPrompt("hello") == "No active ACP session (see acp-start-session).");
}

TEST_CASE("AcpManager::SendPrompt sends session/prompt and streams the stop reason back into the output buffer", "[Acp]") {
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

TEST_CASE("AcpManager streams an agent_message_chunk session/update into the output buffer", "[Acp]") {
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

TEST_CASE("AcpManager answers fs/read_text_file from an open buffer's live content, not disk", "[Acp]") {
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

TEST_CASE("AcpManager's fs/write_text_file writes to disk and merges into an open, unmodified buffer via Revert", "[Acp]") {
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

TEST_CASE("AcpManager routes session/request_permission to the registered handler and answers a selected option", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    AcpManager::PermissionPrompt captured;
    bool                         handlerCalled = false;
    fixture.manager.SetOnPermissionRequest([&](const AcpManager::PermissionPrompt& prompt) {
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

TEST_CASE("AcpManager::StopSession tears the session down even with no active session", "[Acp]") {
    ManagerFixture fixture;
    REQUIRE(fixture.manager.StopSession() == "No active ACP session.");
}

TEST_CASE("AcpManager::StopSession sends session/close and reaches Inactive", "[Acp]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("test-agent");

    REQUIRE(fixture.manager.StopSession() == "ACP session stopped.");
    REQUIRE(fixture.manager.State() == AcpManager::SessionState::Inactive);

    const Json closeRequest = fixture.reader.Next();
    REQUIRE(closeRequest["method"] == "session/close");
}
