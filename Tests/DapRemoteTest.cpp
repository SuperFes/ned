#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Dap/Manager.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteDapAdapter.h"
#include "FakeRemoteHost.h"
#include "Text/RemoteFile.h"
#include "UI/EventLoop.h"

namespace fs     = std::filesystem;
namespace dap    = ned::editor::dap;
namespace remote = ned::editor::remote;
namespace text   = ned::text;
using Json       = nlohmann::json;
using ned::tests::PumpUntil;

namespace {

constexpr const char* kLanguage = ned::tests::kRemoteDapLanguage;

struct Fixture {
    ned::tests::FakeRemoteHost       fake;
    ned::ui::EventLoop               loop;
    remote::Hosts                    hosts{loop, [this](const std::string& destination) { return fake.Options(destination); }, ned::tests::FakeResolver(fake.dir)};
    ned::tests::FakeRemoteDapAdapter adapter{fake.dir};
    dap::Manager                     manager{loop};

    Fixture() {
        manager.SetRemoteHosts(&hosts);
    }
    [[nodiscard]] text::RemoteFile File() const {
        return {.destination = "host", .path = adapter.file};
    }
};

} // namespace

TEST_CASE("A remote buffer's debug adapter runs on its host, in the project root there", "[Dap][DapRemote][HostAgent]") {
    Fixture f;
    f.manager.ToggleBreakpoint(f.File(), 2);
    f.manager.ToggleBreakpoint(f.fake.dir / "here.c", 5);

    REQUIRE(f.manager.StartOrContinue(kLanguage, f.File()) == std::string("Starting debug session (") + kLanguage + ") on host...");
    REQUIRE(f.manager.State() == dap::Manager::SessionState::Starting);
    REQUIRE(PumpUntil(f.loop, [&] { return f.manager.State() == dap::Manager::SessionState::Stopped; }));

    const std::vector<Json> messages = f.adapter.Messages();
    REQUIRE(!messages.empty());
    CHECK(messages.front()["cwd"] == f.adapter.project.string());
    REQUIRE(f.adapter.Requests("launch").size() == 1);
    CHECK(f.adapter.Requests("launch").front()["arguments"]["program"] == "a.out");
    // Only the host's file: the local one means nothing to this adapter.
    const std::vector<Json> setBreakpoints = f.adapter.Requests("setBreakpoints");
    REQUIRE(setBreakpoints.size() == 1);
    CHECK(setBreakpoints.front()["arguments"]["source"]["path"] == f.adapter.file.string());
    CHECK(setBreakpoints.front()["arguments"]["breakpoints"] == Json::array({Json{{"line", 2}}}));

    // A breakpoint toggled while it runs goes to the host as well.
    f.manager.ToggleBreakpoint(f.File(), 1);
    REQUIRE(PumpUntil(f.loop, [&] { return f.adapter.Requests("setBreakpoints").size() == 2; }));
    CHECK(f.adapter.Requests("setBreakpoints").back()["arguments"]["breakpoints"] == Json::array({Json{{"line", 1}}, Json{{"line", 2}}}));

    CHECK(f.manager.StopSession() == "Debug session stopped.");
    CHECK(f.manager.State() == dap::Manager::SessionState::Inactive);
}

TEST_CASE("A debug session stopped before its adapter starts on the host never starts it", "[Dap][DapRemote][HostAgent]") {
    Fixture f;
    REQUIRE(f.manager.StartOrContinue(kLanguage, f.File()).ends_with("on host..."));
    REQUIRE(f.manager.StopSession() == "Debug session stopped.");

    CHECK_FALSE(PumpUntil(f.loop, [&] { return fs::exists(f.adapter.log); }, std::chrono::milliseconds(1500)));
    CHECK(f.manager.State() == dap::Manager::SessionState::Inactive);
}

TEST_CASE("A debug session on a host fails at once when there are no hosts", "[Dap][DapRemote]") {
    ned::ui::EventLoop               loop;
    const fs::path                   dir = ned::tests::FreshRemoteDir();
    ned::tests::FakeRemoteDapAdapter adapter{dir};
    dap::Manager                     manager{loop};
    std::string                      ended;
    manager.SetOnSessionEnded([&](std::string reason) { ended = std::move(reason); });

    CHECK(manager.StartOrContinue(kLanguage, text::RemoteFile{.destination = "host", .path = adapter.file}) ==
          "Debug session failed to start.");
    CHECK(manager.State() == dap::Manager::SessionState::Inactive);
    CHECK(ended.starts_with("Failed to start debug adapter: no remote host"));
}
