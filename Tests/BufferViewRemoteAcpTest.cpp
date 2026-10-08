//
// ACP commands typed into a BufferView on a remote buffer, against an
// agent on a fake host (FakeRemoteAcpAgent.h).
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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
