//
// Debugger commands typed into a BufferView on a remote buffer, against a
// debug adapter on a fake host (FakeRemoteDapAdapter.h).
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <utility>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dap/Manager.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteDapAdapter.h"
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
namespace dap  = ned::editor::dap;
namespace test = ned::ui::test;
namespace text = ned::text;
using Json     = nlohmann::json;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost       fake;
    ned::tests::ScopedEnvOverride    home{"HOME", (fake.dir / "home").c_str()};
    ned::ui::EventLoop               loop;
    ned::editor::remote::Hosts       hosts{loop, [this](const std::string& destination) { return fake.Options(destination); },
                                           ned::tests::FakeResolver(fake.dir)};
    ned::tests::FakeRemoteDapAdapter adapter{fake.dir};

    text::Buffer                 buffer{"scratch"};
    text::KillRing               killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    text::BufferList             bufferList;
    dap::Manager                 dapManager{loop};
    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode = [] {
        ned::editor::Mode m = ned::editor::FundamentalMode();
        m.name              = std::string(ned::tests::kRemoteDapLanguage) + "-mode";
        return m;
    }();
    ned::ui::Theme theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};
    ned::ui::BufferView   view{activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme};

    Fixture() {
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
        view.SetRemoteHosts(&hosts);
        view.SetDapManager(&dapManager);
        dapManager.SetRemoteHosts(&hosts);
        // What WindowManager does with a stop.
        dapManager.SetOnStopped([this](const dap::Manager::StoppedInfo& info) {
            if (info.file) {
                view.JumpToFileLine(*info.file, info.line);
            }
        });
    }

    void Command(const std::string& name) {
        view.OnEvent(test::Alt('x'));
        for (const char c : name) {
            view.OnEvent(test::Character(std::string(1, c)));
        }
        view.OnEvent(test::Return());
    }
    [[nodiscard]] text::RemoteFile File() const {
        return {.destination = "host", .path = adapter.file};
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

TEST_CASE("dap-continue on a remote buffer debugs on its host and stops in the host's file", "[BufferView][Remote][Dap][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    text::Buffer& file = fixture.activeBuffer.Get();
    file.SetPoint(std::string("int x;\n").size());
    fixture.Command("dap-toggle-breakpoint");
    CHECK(fixture.dapManager.BreakpointsForFile(fixture.File()) == std::vector<std::size_t>{2});

    fixture.Command("dap-continue");
    // Somewhere else when it stops, so the stop has to bring the file back.
    fixture.activeBuffer.Set(fixture.buffer);
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.dapManager.CurrentStopKeyAndLine().has_value() && fixture.Showing(); }));

    const std::vector<Json> setBreakpoints = fixture.adapter.Requests("setBreakpoints");
    REQUIRE(setBreakpoints.size() == 1);
    CHECK(setBreakpoints.front()["arguments"]["source"]["path"] == fixture.adapter.file.string());
    CHECK(fixture.Showing());
    CHECK(fixture.activeBuffer.Get().Point() == std::string("int x;\n").size());
    const auto stop = fixture.dapManager.CurrentStopKeyAndLine();
    REQUIRE(stop.has_value());
    CHECK(stop->first == dap::Manager::NormalizePathKey(fixture.File()));
    CHECK(stop->second == 2);
    fixture.dapManager.StopSession();
}

TEST_CASE("A host's debug session lists its frames as host:/path lines", "[BufferView][Remote][Dap][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    fixture.Command("dap-continue");
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.dapManager.State() == dap::Manager::SessionState::Stopped; }));

    fixture.Command("dap-show-debug");
    const std::string frame = fixture.File().ToString() + ":2: #0 main";
    REQUIRE(PumpUntil(fixture.loop, [&] {
        const text::Buffer* debug = std::as_const(fixture.bufferList).Find("*debug*");
        return debug != nullptr && debug->Content().ToString().find(frame) != std::string::npos;
    }));
    fixture.dapManager.StopSession();
}
