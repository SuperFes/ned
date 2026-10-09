#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

TEST_CASE("A host's debug session launches what its root's settings there say, ${workspaceFolder} and all", "[Dap][DapRemote][HostAgent]") {
    Fixture f;
    std::filesystem::create_directories(f.adapter.project / ".ned");
    std::ofstream(f.adapter.project / ".ned" / "settings.json")
        << R"({"dapLaunch": {")" << kLanguage << R"(": {"program": "${workspaceFolder}/build/app"}}})";

    REQUIRE(f.manager.StartOrContinue(kLanguage, f.File()) == std::string("Starting debug session (") + kLanguage + ") on host...");
    REQUIRE(PumpUntil(f.loop, [&] { return f.adapter.Requests("launch").size() == 1; }));
    CHECK(f.adapter.Requests("launch").front()["arguments"]["program"] == (f.adapter.project / "build" / "app").string());
    f.manager.StopSession();
}

TEST_CASE("A host's debug adapter reports where it stopped as files on that host", "[Dap][DapRemote][HostAgent]") {
    Fixture                                  f;
    std::optional<dap::Manager::StoppedInfo> stopped;
    f.manager.SetOnStopped([&](const dap::Manager::StoppedInfo& info) { stopped = info; });

    REQUIRE(f.manager.StartOrContinue(kLanguage, f.File()).ends_with("on host..."));
    REQUIRE(PumpUntil(f.loop, [&] { return stopped.has_value(); }));
    CHECK(stopped->file == std::optional<text::FileRef>(f.File()));
    CHECK(stopped->line == 2);
    CHECK(f.manager.CurrentStopKeyAndLine() == std::make_optional(std::make_pair(f.File().ToString(), std::size_t{2})));

    std::optional<std::vector<dap::Manager::StackFrame>> frames;
    f.manager.RequestStackTrace([&](std::vector<dap::Manager::StackFrame> answer) { frames = std::move(answer); });
    REQUIRE(PumpUntil(f.loop, [&] { return frames.has_value(); }));
    REQUIRE(frames->size() == 1);
    CHECK(frames->front().file == std::optional<text::FileRef>(f.File()));
    f.manager.StopSession();
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

// A real adapter against a real debugger: vscode-php-debug's phpDebug.js
// (NED_TEST_PHP_DEBUG) launching PHP with Xdebug (NED_TEST_XDEBUG, the
// zend_extension to load), both on the fake host, reached through proc.
TEST_CASE("A PHP script on a host debugs through Xdebug, with the adapter running there", "[Dap][DapRemote][Xdebug][HostAgent]") {
    const char* xdebug   = std::getenv("NED_TEST_XDEBUG");
    const char* phpDebug = std::getenv("NED_TEST_PHP_DEBUG");
    if (xdebug == nullptr || phpDebug == nullptr) {
        SKIP("NED_TEST_XDEBUG and NED_TEST_PHP_DEBUG name Xdebug and vscode-php-debug's phpDebug.js");
    }
    ned::tests::FakeRemoteHost fake;
    ned::ui::EventLoop         loop;
    remote::Hosts              hosts{loop, [&fake](const std::string& destination) { return fake.Options(destination); }, ned::tests::FakeResolver(fake.dir)};
    dap::Manager               manager{loop};
    manager.SetRemoteHosts(&hosts);
    const fs::path project = fake.dir / "site";
    fs::create_directories(project / ".git");
    std::ofstream(project / "index.php") << "<?php\n$a = 20;\n$b = $a + 22;\necho $b, \"\\n\";\n";
    const text::RemoteFile script{.destination = "host", .path = fs::canonical(project) / "index.php"};

    struct Restore {
        ~Restore() {
            dap::SetAdapterCommand("php", {});
            dap::SetLaunchConfig("php", "");
        }
    } restore;
    dap::SetAdapterCommand("php", {"node", phpDebug});
    dap::SetLaunchConfig("php", Json{{"program", "${workspaceFolder}/index.php"},
                                     {"runtimeExecutable", "php"},
                                     // The adapter fills in ${port}, the one it listens on there.
                                     {"runtimeArgs", Json::array({"-n", std::string("-dzend_extension=") + xdebug, "-dxdebug.mode=debug",
                                                                  "-dxdebug.start_with_request=yes", "-dxdebug.client_port=${port}"})},
                                     {"port", 0}}
                                    .dump());
    std::optional<dap::Manager::StoppedInfo> stopped;
    std::optional<std::string>               ended;
    manager.SetOnStopped([&](const dap::Manager::StoppedInfo& info) { stopped = info; });
    manager.SetOnSessionEnded([&](std::string reason) { ended = std::move(reason); });
    manager.ToggleBreakpoint(script, 3);

    REQUIRE(manager.StartOrContinue("php", script).ends_with("on host..."));
    REQUIRE(PumpUntil(loop, [&] { return stopped.has_value() || ended.has_value(); }, std::chrono::seconds(60)));
    INFO(ended.value_or(""));
    REQUIRE(stopped.has_value());
    CHECK(stopped->file == std::optional<text::FileRef>(script));
    CHECK(stopped->line == 3);

    // The host's PHP holds $a as the script set it.
    std::optional<std::vector<dap::Manager::StackFrame>> frames;
    manager.RequestStackTrace([&](std::vector<dap::Manager::StackFrame> answer) { frames = std::move(answer); });
    REQUIRE(PumpUntil(loop, [&] { return frames.has_value(); }));
    REQUIRE_FALSE(frames->empty());
    std::optional<std::vector<dap::Manager::Scope>> scopes;
    manager.RequestScopes(frames->front().id, [&](std::vector<dap::Manager::Scope> answer) { scopes = std::move(answer); });
    REQUIRE(PumpUntil(loop, [&] { return scopes.has_value(); }));
    REQUIRE_FALSE(scopes->empty());
    std::optional<std::vector<dap::Manager::Variable>> variables;
    manager.RequestVariables(scopes->front().variablesReference, [&](std::vector<dap::Manager::Variable> answer) { variables = std::move(answer); });
    REQUIRE(PumpUntil(loop, [&] { return variables.has_value(); }));
    const auto a = std::ranges::find(*variables, std::string("$a"), &dap::Manager::Variable::name);
    REQUIRE(a != variables->end());
    CHECK(a->value == "20");

    // Run to the end: the script finishes on the host.
    manager.StartOrContinue("php", script);
    REQUIRE(PumpUntil(loop, [&] { return ended.has_value(); }, std::chrono::seconds(60)));
}
