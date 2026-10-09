#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Acp/Manager.h"
#include "Editor/Dap/Manager.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Mcp/BridgeServer.h"
#include "Editor/Mcp/ToolRegistry.h"
#include "Editor/Process/ChildProcess.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/Runner.h"
#include "FakeRemoteAcpAgent.h"
#include "FakeRemoteHost.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/RemoteFile.h"
#include "Text/Rope.h"
#include "UI/EventLoop.h"

namespace fs     = std::filesystem;
namespace acp    = ned::editor::acp;
namespace remote = ned::editor::remote;
namespace text   = ned::text;
using Json       = nlohmann::json;
using ned::tests::PumpUntil;

namespace {

constexpr const char* kAgent = ned::tests::kRemoteAcpAgent;

struct Fixture {
    ned::tests::FakeRemoteHost     fake;
    ned::ui::EventLoop             loop;
    remote::Hosts                  hosts{loop, [this](const std::string& destination) { return fake.Options(destination); }, ned::tests::FakeResolver(fake.dir)};
    ned::tests::FakeRemoteAcpAgent agent{fake.dir};
    text::BufferList               buffers;
    acp::Manager                   manager{buffers, loop};

    Fixture() {
        manager.SetRemoteHosts(&hosts);
    }
    [[nodiscard]] text::RemoteFile File() const {
        return {.destination = "host", .path = agent.file};
    }
    bool Start() {
        manager.StartSession(kAgent, File());
        return PumpUntil(loop, [&] { return manager.State() == acp::Manager::SessionState::Active; });
    }
    [[nodiscard]] bool Said(const std::string& text) const {
        const auto& transcript = manager.Transcript();
        return std::any_of(transcript.begin(), transcript.end(), [&](const acp::Manager::TranscriptEntry& entry) { return entry.text.find(text) != std::string::npos; });
    }
};

} // namespace

TEST_CASE("A remote buffer's ACP agent runs on its host, in the project root there", "[Acp][AcpRemote][HostAgent]") {
    Fixture f;
    f.manager.StartSession(kAgent, f.File());
    CHECK(f.manager.State() == acp::Manager::SessionState::Starting);
    CHECK(f.manager.AgentBeside() == std::optional<text::RemoteFile>(f.File()));
    REQUIRE(PumpUntil(f.loop, [&] { return f.manager.State() == acp::Manager::SessionState::Active; }));

    const std::vector<Json> messages = f.agent.Messages();
    REQUIRE(!messages.empty());
    CHECK(messages.front()["cwd"] == f.agent.project.string());
    const std::vector<Json> created = f.agent.Requests("session/new");
    REQUIRE(created.size() == 1);
    CHECK(created.front()["params"]["cwd"] == f.agent.project.string());
    CHECK(created.front()["params"]["mcpServers"] == Json::array());

    CHECK(f.manager.SendPrompt("hello") == "Sent.");
    REQUIRE(PumpUntil(f.loop, [&] { return !f.manager.PromptInFlight() && f.Said("done"); }));

    CHECK(f.manager.StopSession() == "ACP session stopped.");
    CHECK(f.manager.State() == acp::Manager::SessionState::Inactive);
    CHECK_FALSE(f.manager.AgentBeside().has_value());
}

TEST_CASE("A host's ACP agent gets the additional directories its root's settings name there", "[Acp][AcpRemote][HostAgent]") {
    Fixture f;
    fs::create_directories(f.agent.project / ".ned");
    std::ofstream(f.agent.project / ".ned" / "settings.json") << R"({"acpAdditionalDirectories": ["../shared", "/srv/other"]})";
    REQUIRE(f.Start());

    const std::vector<Json> created = f.agent.Requests("session/new");
    REQUIRE(created.size() == 1);
    CHECK(created.front()["params"]["additionalDirectories"] == Json::array({(f.agent.project.parent_path() / "shared").string(), "/srv/other"}));
}

TEST_CASE("A host's ACP agent lists its sessions in the host's project root", "[Acp][AcpRemote][HostAgent]") {
    Fixture f;
    REQUIRE(f.Start());
    std::optional<std::string> error;
    f.manager.ListSessions([&](std::vector<acp::Manager::SessionSummary>, std::string message) { error = std::move(message); });
    REQUIRE(PumpUntil(f.loop, [&] { return error.has_value(); }));
    CHECK(error->empty());
    const std::vector<Json> listed = f.agent.Requests("session/list");
    REQUIRE(listed.size() == 1);
    CHECK(listed.front()["params"]["cwd"] == f.agent.project.string());
    f.manager.StopSession();
}

TEST_CASE("An ACP session stopped before its agent starts on the host never starts it", "[Acp][AcpRemote][HostAgent]") {
    Fixture f;
    f.manager.StartSession(kAgent, f.File());
    REQUIRE(f.manager.StopSession() == "ACP session stopped.");

    CHECK_FALSE(PumpUntil(f.loop, [&] { return fs::exists(f.agent.log); }, std::chrono::milliseconds(1500)));
    CHECK(f.manager.State() == acp::Manager::SessionState::Inactive);
}

TEST_CASE("An ACP agent on a host fails at once when there are no hosts", "[Acp][AcpRemote]") {
    ned::ui::EventLoop             loop;
    const fs::path                 dir = ned::tests::FreshRemoteDir();
    ned::tests::FakeRemoteAcpAgent agent{dir};
    text::BufferList               buffers;
    acp::Manager                   manager{buffers, loop};
    std::string                    ended;
    manager.SetOnSessionEnded([&](std::string reason) { ended = std::move(reason); });

    manager.StartSession(kAgent, text::RemoteFile{.destination = "host", .path = agent.file});
    CHECK(manager.State() == acp::Manager::SessionState::Inactive);
    CHECK(ended.starts_with("Failed to start ACP agent: no remote host"));
    CHECK_FALSE(manager.AgentBeside().has_value());
}

TEST_CASE("A host's ACP agent reads its files from the host", "[Acp][AcpRemote][HostAgent]") {
    Fixture f;
    REQUIRE(f.Start());
    const Json script = Json::array({Json{{"method", "fs/read_text_file"}, {"params", {{"sessionId", "s1"}, {"path", f.agent.file.string()}}}},
                                     Json{{"method", "fs/read_text_file"}, {"params", {{"sessionId", "s1"}, {"path", (f.agent.project / "gone.c").string()}}}}});
    CHECK(f.manager.SendPrompt(script.dump()) == "Sent.");
    REQUIRE(PumpUntil(f.loop, [&] { return !f.manager.PromptInFlight() && f.Said("done"); }));

    const std::vector<Json> answers = f.agent.Answers();
    REQUIRE(answers.size() == 2);
    CHECK(answers[0]["result"]["content"] == "int x;\nint y;\n");
    CHECK(answers[1].contains("error"));
    f.manager.StopSession();
}

TEST_CASE("A host's ACP agent writes its files on the host", "[Acp][AcpRemote][HostAgent]") {
    Fixture        f;
    const fs::path created = f.agent.project / "src" / "b.c";
    REQUIRE(f.Start());
    const Json script = Json::array({Json{{"method", "fs/write_text_file"}, {"params", {{"sessionId", "s1"}, {"path", created.string()}, {"content", "int b;\n"}}}}});
    CHECK(f.manager.SendPrompt(script.dump()) == "Sent.");
    REQUIRE(PumpUntil(f.loop, [&] { return !f.manager.PromptInFlight() && f.Said("done"); }));

    const std::vector<Json> answers = f.agent.Answers();
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]["result"] == Json::object());
    std::ifstream in(created);
    CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == "int b;\n");
    // Not open here: tracked from a read on the host ahead of the write,
    // which found nothing there yet.
    REQUIRE(f.manager.CheckpointCount() == 1);
    CHECK(f.manager.CheckpointAt(0).untrackedFiles.empty());
    REQUIRE(f.manager.CheckpointAt(0).files.size() == 1);
    CHECK(f.manager.CheckpointAt(0).files[0].file == text::FileRef(text::RemoteFile{.destination = "host", .path = created}));
    CHECK_FALSE(f.manager.CheckpointAt(0).files[0].before.exists);
    f.manager.StopSession();
}

TEST_CASE("The paths a host's ACP agent names are files on that host", "[Acp][AcpRemote][HostAgent]") {
    Fixture                                   f;
    std::optional<acp::Manager::ToolLocation> reached;
    f.manager.SetOnToolLocation([&](const acp::Manager::ToolLocation& location) { reached = location; });
    REQUIRE(f.Start());
    CHECK(f.manager.TranscriptRoot() == f.agent.project);

    const Json toolCall{{"sessionUpdate", "tool_call"},
                        {"toolCallId", "t1"},
                        {"title", "Read a.c"},
                        {"kind", "read"},
                        {"status", "in_progress"},
                        {"locations", Json::array({Json{{"path", f.agent.file.string()}, {"line", 1}}})}};
    const Json script = Json::array({Json{{"method", "session/update"}, {"notify", true}, {"params", {{"sessionId", "s1"}, {"update", toolCall}}}}});
    CHECK(f.manager.SendPrompt(script.dump()) == "Sent.");
    REQUIRE(PumpUntil(f.loop, [&] { return !f.manager.PromptInFlight() && f.Said("done"); }));
    REQUIRE(reached.has_value());
    CHECK(f.manager.FileFor(reached->path) == text::FileRef(f.File()));

    // The conversation stays on the host's files once the agent has gone.
    f.manager.StopSession();
    CHECK(f.manager.FileFor(f.agent.file.string()) == text::FileRef(f.File()));
    CHECK(f.manager.TranscriptRoot() == f.agent.project);
}

TEST_CASE("A host's ACP agent reaches ned's MCP tools through a relay on its host", "[Acp][AcpRemote][Mcp][HostAgent]") {
    Fixture                          f;
    ned::editor::lsp::Manager        lsp{f.buffers, f.loop};
    ned::editor::vcs::Runner         vcs{f.loop};
    ned::editor::testrun::TestRunner tests{f.buffers, f.loop};
    ned::editor::dap::Manager        dap{f.loop};
    ned::editor::mcp::ToolRegistry   registry{f.buffers, lsp, vcs, tests, dap};
    ned::editor::mcp::BridgeServer   bridge{registry, f.loop};
    f.manager.SetMcpBridgeServer(&bridge);
    // The host's file, open here with a finding on it.
    text::Buffer& open = f.buffers.AddRemotePlaceholder(f.File());
    open.FinishLoad(text::Rope("int x;\nint y;\n"));
    open.SetDiagnostics({text::Buffer::Diagnostic{.startByte = 4, .endByte = 5, .severity = text::Buffer::Diagnostic::Severity::Error, .message = "found on the host"}});
    REQUIRE(f.Start());

    const std::vector<Json> created = f.agent.Requests("session/new");
    REQUIRE(created.size() == 1);
    const Json& servers = created.front()["params"]["mcpServers"];
    REQUIRE(servers.size() == 1);
    CHECK(servers[0]["args"][0] == "--mcp-relay");

    // What the agent would run there; the fake host is this machine.
    std::vector<std::string> argv{servers[0]["command"].get<std::string>()};
    for (const Json& arg : servers[0]["args"]) {
        argv.push_back(arg.get<std::string>());
    }
    ned::editor::process::ChildProcess relay(argv);
    std::atomic<bool>                  done{false};
    std::vector<Json>                  answers;
    std::thread                        client([&] {
        const auto ask = [&](const Json& request) {
            relay.WriteAll(request.dump() + "\n", std::chrono::seconds(5));
            std::string line;
            while (line.find('\n') == std::string::npos) {
                const std::string chunk = relay.ReadSome();
                if (chunk.empty()) {
                    return;
                }
                line += chunk;
            }
            answers.push_back(Json::parse(line.substr(0, line.find('\n'))));
        };
        ask(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", "2025-06-18"}}}});
        ask(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"}, {"params", {{"name", "get_diagnostics"}, {"arguments", {{"file", f.agent.file.string()}}}}}});
        done = true;
    });
    const bool                         finished = PumpUntil(f.loop, [&] { return done.load(); });
    client.join();
    REQUIRE(finished);
    REQUIRE(answers.size() == 2);
    CHECK(answers[0]["result"]["serverInfo"]["name"] == "ned");
    const std::string text = answers[1]["result"]["content"][0]["text"].get<std::string>();
    CHECK(text.find("found on the host") != std::string::npos);
    f.manager.StopSession();
}
