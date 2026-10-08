//
// ACP commands typed into a BufferView on a remote buffer, against an
// agent on a fake host (FakeRemoteAcpAgent.h).
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Acp/Manager.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteAcpAgent.h"
#include "FakeRemoteHost.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using Catch::Matchers::ContainsSubstring;
using ned::tests::PumpUntil;
namespace acp  = ned::editor::acp;
namespace test = ned::ui::test;
namespace text = ned::text;
using Json     = nlohmann::json;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost     fake;
    ned::tests::ScopedEnvOverride  home{"HOME", (fake.dir / "home").c_str()};
    ned::ui::EventLoop             loop;
    ned::editor::remote::Hosts     hosts{loop, [this](const std::string& destination) { return fake.Options(destination); },
                                         ned::tests::FakeResolver(fake.dir)};
    ned::tests::FakeRemoteAcpAgent agent{fake.dir};

    text::Buffer                 buffer{"scratch"};
    text::KillRing               killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    text::BufferList             bufferList;
    acp::Manager                 acpManager{bufferList, loop};
    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode = [] {
        ned::editor::Mode m = ned::editor::FundamentalMode();
        m.name              = "c-mode";
        return m;
    }();
    ned::ui::Theme theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};
    ned::ui::BufferView   view{activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme};

    Fixture() {
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
        view.SetRemoteHosts(&hosts);
        view.SetAcpManager(&acpManager);
        acpManager.SetRemoteHosts(&hosts);
    }

    void Command(const std::string& name) {
        view.OnEvent(test::Alt('x'));
        for (const char c : name) {
            view.OnEvent(test::Character(std::string(1, c)));
        }
        view.OnEvent(test::Return());
    }
    [[nodiscard]] text::RemoteFile File() const {
        return {.destination = "host", .path = agent.file};
    }
    [[nodiscard]] bool Showing() const {
        const text::Buffer& shown = activeBuffer.Get();
        return shown.Remote() == File() && !shown.IsLoading();
    }
    bool OpenFile() {
        view.VisitRemoteFile(File());
        return PumpUntil(loop, [&] { return Showing(); });
    }
};

} // namespace

TEST_CASE("acp-start-session on a remote buffer runs the agent on its host", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    fixture.Command("acp-start-session");
    for (const char c : std::string(ned::tests::kRemoteAcpAgent)) {
        fixture.view.OnEvent(test::Character(std::string(1, c)));
    }
    fixture.view.OnEvent(test::Return());

    CHECK(fixture.acpManager.AgentBeside() == std::optional<text::RemoteFile>(fixture.File()));
    CHECK_THAT(fixture.activeBuffer.Get().Name(), ContainsSubstring("*acp: "));
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.acpManager.State() == acp::Manager::SessionState::Active; }));
    CHECK(fixture.agent.Requests("session/new").front()["params"]["cwd"] == fixture.agent.project.string());
    fixture.acpManager.StopSession();
}

namespace {

std::string WriteScript(const text::RemoteFile& file, const std::string& content) {
    return Json::array({Json{{"method", "fs/write_text_file"}, {"params", {{"sessionId", "s1"}, {"path", file.path.string()}, {"content", content}}}}}).dump();
}

bool Start(Fixture& fixture) {
    fixture.acpManager.StartSession(ned::tests::kRemoteAcpAgent, fixture.File());
    return PumpUntil(fixture.loop, [&] { return fixture.acpManager.State() == acp::Manager::SessionState::Active; });
}

bool Turn(Fixture& fixture, const std::string& prompt) {
    const std::size_t turns = fixture.acpManager.CheckpointCount();
    return fixture.acpManager.SendPrompt(prompt) == "Sent." &&
           PumpUntil(fixture.loop, [&] { return !fixture.acpManager.PromptInFlight() && fixture.acpManager.CheckpointCount() > turns; });
}

} // namespace

TEST_CASE("A host's ACP agent reads an open buffer's own text", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    file.SetPoint(file.Text().size());
    file.InsertAtPoint("int z;\n");
    REQUIRE(Start(fixture));

    REQUIRE(Turn(fixture, Json::array({Json{{"method", "fs/read_text_file"}, {"params", {{"sessionId", "s1"}, {"path", fixture.agent.file.string()}}}}}).dump()));
    const std::vector<Json> answers = fixture.agent.Answers();
    REQUIRE(answers.size() == 1);
    CHECK(answers[0]["result"]["content"] == "int x;\nint y;\nint z;\n");
    fixture.acpManager.StopSession();
}

TEST_CASE("A host's ACP agent's write reaches the open buffer, and rewinds", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    REQUIRE(Start(fixture));

    REQUIRE(Turn(fixture, WriteScript(fixture.File(), "int x;\nint w;\n")));
    std::ifstream in(fixture.agent.file);
    CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == "int x;\nint w;\n");
    CHECK(file.Text() == "int x;\nint w;\n");
    CHECK_FALSE(file.Modified());
    REQUIRE(fixture.acpManager.CheckpointAt(0).fileRecords.size() == 1);
    CHECK(fixture.acpManager.CheckpointAt(0).fileRecords[0].file == text::FileRef(fixture.File()));

    const acp::Manager::RewindOutcome outcome = fixture.acpManager.RewindTo(0);
    CHECK(outcome.revertedFiles == std::vector<std::string>{fixture.File().ToString()});
    CHECK(file.Text() == "int x;\nint y;\n");
    fixture.acpManager.StopSession();
}

TEST_CASE("A host's ACP agent's write merges into a buffer with changes of its own", "[BufferView][Remote][Acp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    file.SetPoint(file.Text().size());
    file.InsertAtPoint("int z;\n");
    REQUIRE(Start(fixture));

    REQUIRE(Turn(fixture, WriteScript(fixture.File(), "int x;\nint w;\n")));
    CHECK(file.Text() == "int x;\nint w;\nint z;\n");
    CHECK(file.Modified());
    fixture.acpManager.StopSession();
}
