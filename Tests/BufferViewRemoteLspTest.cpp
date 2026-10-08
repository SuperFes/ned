//
// LSP commands typed into a BufferView on a remote buffer, against a
// language server on a fake host (FakeRemoteLspServer.h).
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteHost.h"
#include "FakeRemoteLspServer.h"
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
namespace test = ned::ui::test;
namespace text = ned::text;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost      fake;
    ned::tests::ScopedEnvOverride   home{"HOME", (fake.dir / "home").c_str()};
    ned::ui::EventLoop              loop;
    ned::editor::remote::Hosts      hosts{loop, [this](const std::string& destination) { return fake.Options(destination); },
                                          ned::tests::FakeResolver(fake.dir)};
    ned::tests::FakeRemoteLspServer server{fake.dir};

    text::Buffer                 buffer{"scratch"};
    text::KillRing               killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    text::BufferList             bufferList;
    ned::editor::lsp::Manager    lsp{bufferList, loop};
    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode = [] {
        ned::editor::Mode m = ned::editor::FundamentalMode();
        m.name              = std::string(ned::tests::kRemoteLspLanguage) + "-mode";
        return m;
    }();
    ned::ui::Theme theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};
    ned::ui::BufferView   view{activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme};

    Fixture() {
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
        view.SetRemoteHosts(&hosts);
        view.SetLspManager(&lsp);
        lsp.SetRemoteHosts(&hosts);
    }

    void Command(const std::string& name) {
        view.OnEvent(test::Alt('x'));
        for (const char c : name) {
            view.OnEvent(test::Character(std::string(1, c)));
        }
        view.OnEvent(test::Return());
    }
    [[nodiscard]] bool Showing(const std::filesystem::path& path) const {
        const text::Buffer& shown = activeBuffer.Get();
        return shown.Remote() == text::RemoteFile{.destination = "host", .path = path} && !shown.IsLoading();
    }
    // Opens a.c from the host and waits for its server to have it.
    bool OpenFile() {
        view.VisitRemoteFile({.destination = "host", .path = server.file});
        return PumpUntil(loop, [&] { return Showing(server.file); }) && PumpUntil(loop, [&] {
                   lsp.SyncBuffer(activeBuffer.Get(), ned::tests::kRemoteLspLanguage);
                   return server.Find("textDocument/didOpen").has_value();
               });
    }
};

} // namespace

TEST_CASE("lsp-goto-definition on a remote buffer opens the host's file at the definition", "[BufferView][Remote][Lsp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    fixture.activeBuffer.Get().SetPoint(4);

    fixture.Command("lsp-goto-definition");
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.Showing(fixture.server.other); }));
    CHECK(fixture.activeBuffer.Get().Point() == std::string("// b\n").size() + 4);
}

TEST_CASE("project-find-references on a remote buffer lists the host's references", "[BufferView][Remote][Lsp][HostAgent]") {
    Fixture fixture;
    REQUIRE(fixture.OpenFile());
    fixture.activeBuffer.Get().SetPoint(4);

    fixture.Command("project-find-references");
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.activeBuffer.Get().Name().starts_with("*references: x*"); }));
    const std::string results = fixture.activeBuffer.Get().Text();
    CHECK_THAT(results, ContainsSubstring("host:" + fixture.server.file.string() + ":1: int x;\n"));
    // Not open here, so there's no text to show for it.
    CHECK_THAT(results, ContainsSubstring("host:" + fixture.server.other.string() + ":2: \n"));
    CHECK(fixture.statusMessage == "2 references to \"x\"");

    fixture.Command("next-error");
    CHECK(fixture.Showing(fixture.server.file));
    fixture.Command("next-error");
    REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.Showing(fixture.server.other); }));
    CHECK(fixture.activeBuffer.Get().Point() == std::string("// b\n").size());
}
