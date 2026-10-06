//
// remote-connect, remote-disconnect and remote-project-search, typed into a
// BufferView the way a user runs them, against ned_fake_ssh and the real
// agent.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PostQueue.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteHost.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/ListPopup.h"
#include "UI/Theme.h"

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using ned::tests::PumpUntil;
using ned::ui::BufferView;
namespace test = ned::ui::test;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost fake;
    ned::editor::PostQueue     queue;
    ned::editor::remote::Hosts hosts{queue, [this](const std::string& destination) { return fake.Options(destination); }};

    ned::text::Buffer            buffer{"scratch"};
    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};
    BufferView            view{activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme};

    Fixture() {
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
        view.SetRemoteHosts(&hosts);
    }

    void Type(const std::string& text) {
        for (const char c : text) {
            view.OnEvent(test::Character(std::string(1, c)));
        }
    }
    void Enter(const std::string& text) {
        Type(text);
        view.OnEvent(test::Return());
    }
    void Command(const std::string& name) {
        view.OnEvent(test::Alt('x'));
        Enter(name);
    }
    bool StatusStartsWith(const std::string& prefix) {
        return PumpUntil(queue, [&] { return statusMessage.starts_with(prefix); });
    }
};

} // namespace

TEST_CASE("remote-project-search connects, then streams matches into a results buffer", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path tree = fixture.fake.dir / "tree";
    std::filesystem::create_directories(tree / "sub");
    std::ofstream(tree / "a.txt") << "needle one\nhay\nneedle two\n";
    std::ofstream(tree / "sub" / "b.txt") << "a needle\n";

    fixture.Command("remote-project-search");
    fixture.Enter("host:" + tree.string() + "/");
    fixture.Enter("needle");
    REQUIRE(fixture.statusMessage == "Connecting to host...");
    REQUIRE(fixture.StatusStartsWith("3 matches for \"needle\" on host:" + tree.string() + " in "));

    const ned::text::Buffer& results = fixture.activeBuffer.Get();
    REQUIRE(results.Name().starts_with("*search results*"));
    REQUIRE(results.ReadOnly());
    const std::string text = results.Text();
    REQUIRE_THAT(text, ContainsSubstring("host:" + (tree / "a.txt").string() + ":1: needle one\n"));
    REQUIRE_THAT(text, ContainsSubstring("host:" + (tree / "a.txt").string() + ":3: needle two\n"));
    REQUIRE_THAT(text, ContainsSubstring("host:" + (tree / "sub" / "b.txt").string() + ":1: a needle\n"));
    REQUIRE(fixture.hosts.Find("host") != nullptr);

    // Connected now, and the root prompt offers the last one.
    fixture.Command("remote-project-search");
    fixture.view.OnEvent(test::Return());
    fixture.Enter("hay");
    REQUIRE(fixture.statusMessage == "Searching host:" + tree.string() + "...");
    REQUIRE(fixture.StatusStartsWith("1 match for \"hay\""));

    fixture.Command("remote-project-search");
    fixture.view.OnEvent(test::Return());
    fixture.Enter("nothing here");
    REQUIRE(fixture.StatusStartsWith("No matches for \"nothing here\""));
}

TEST_CASE("remote-project-search rejects a bad root or pattern before connecting", "[BufferView][Remote]") {
    Fixture fixture;
    fixture.Command("remote-project-search");
    fixture.Enter("just-a-host");
    REQUIRE(fixture.statusMessage == "Expected host:/path, not \"just-a-host\"");

    fixture.Command("remote-project-search");
    fixture.Enter("host:/tmp");
    fixture.Enter("(");
    REQUIRE_THAT(fixture.statusMessage, StartsWith("Invalid regex: "));
    REQUIRE_FALSE(fixture.hosts.Connecting("host"));
}

TEST_CASE("remote-connect and remote-disconnect report each outcome", "[BufferView][Remote][HostAgent]") {
    Fixture fixture;
    fixture.Command("remote-disconnect");
    REQUIRE(fixture.statusMessage == "Not connected to any host.");

    fixture.Command("remote-connect");
    fixture.Enter("denied");
    REQUIRE(fixture.statusMessage == "Connecting to denied...");
    REQUIRE(fixture.StatusStartsWith("Couldn't connect to denied: "));

    fixture.Command("remote-connect");
    fixture.Enter("host");
    REQUIRE(fixture.StatusStartsWith("Connected to host in "));
    REQUIRE_THAT(fixture.statusMessage, ContainsSubstring("(installed the agent)"));

    fixture.Command("remote-connect");
    fixture.Enter("host");
    REQUIRE(fixture.statusMessage == "Already connected to host");

    // The one live host is offered.
    fixture.Command("remote-disconnect");
    fixture.view.OnEvent(test::Return());
    REQUIRE(fixture.statusMessage == "Disconnected from host");
    REQUIRE(fixture.hosts.Destinations().empty());
}

TEST_CASE("The host prompts offer the hosts ssh knows, and Tab takes one", "[BufferView][Remote]") {
    Fixture                     fixture;
    const std::filesystem::path home = fixture.fake.dir / "home";
    std::filesystem::create_directories(home / ".ssh");
    std::ofstream(home / ".ssh" / "config") << "Host zqalias\n";
    std::ofstream(home / ".ssh" / "known_hosts") << "zqknown.example ssh-ed25519 AAAA\n";
    const ned::tests::ScopedEnvOverride homeOverride("HOME", home.c_str());

    std::optional<ned::ui::ListPopupModel> popup;
    fixture.view.SetOnCandidatesChanged([&](std::optional<ned::ui::ListPopupModel> model) { popup = std::move(model); });

    fixture.Command("remote-connect");
    REQUIRE(popup.has_value());
    // The user's config comes first; the system's own files may add more.
    REQUIRE(popup->rows.at(0).main == "zqalias");
    REQUIRE(popup->rows.at(1).main == "zqknown.example");
    fixture.Type("zqk");
    REQUIRE(popup->rows.size() == 1);
    fixture.view.OnEvent(test::Tab());
    REQUIRE(fixture.statusMessage == "Connect to host: zqknown.example");
    fixture.view.OnEvent(test::Ctrl('g'));
    REQUIRE_FALSE(popup.has_value());

    // The root prompt offers host:/ until the path starts.
    fixture.Command("remote-project-search");
    REQUIRE(popup->rows.at(0).main == "zqalias:/");
    fixture.Type("alias");
    fixture.view.OnEvent(test::Tab());
    REQUIRE(fixture.statusMessage == "Remote search in (host:/path): zqalias:/");
    REQUIRE_FALSE(popup.has_value());
    fixture.Enter("srv");
    REQUIRE(fixture.statusMessage == "Remote search zqalias:/srv for: ");
    REQUIRE_FALSE(popup.has_value());
    fixture.view.OnEvent(test::Ctrl('g'));

    // Roots searched before rank their hosts first, and the prompt starts
    // from the last one.
    fixture.Command("remote-project-search");
    REQUIRE(fixture.statusMessage == "Remote search in (host:/path): zqalias:/srv");
    REQUIRE_FALSE(popup.has_value());
    fixture.view.OnEvent(test::Ctrl('g'));
    fixture.Command("remote-connect");
    REQUIRE(popup->rows.at(0).main == "zqalias");
}
