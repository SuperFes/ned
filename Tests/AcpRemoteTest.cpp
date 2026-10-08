#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Acp/Manager.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteAcpAgent.h"
#include "FakeRemoteHost.h"
#include "Text/BufferList.h"
#include "Text/RemoteFile.h"
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
