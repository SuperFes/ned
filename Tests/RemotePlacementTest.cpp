//
// Where a buffer's processes run: here for a local buffer, and for a remote
// one on its host, in its project root there, against ned_fake_ssh and the
// real agent.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Lifetime.h"
#include "Editor/Process/StreamedChild.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/Placement.h"
#include "Editor/Tasks/TaskConfig.h"
#include "Editor/Tasks/TaskRunner.h"
#include "FakeRemoteHost.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using Catch::Matchers::ContainsSubstring;
using ned::tests::PumpUntil;
namespace fs      = std::filesystem;
namespace process = ned::editor::process;
namespace remote  = ned::editor::remote;
namespace tasks   = ned::editor::tasks;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost fake;
    ned::ui::EventLoop         loop;
    remote::Hosts              hosts{loop, [this](const std::string& destination) { return fake.Options(destination); }, ned::tests::FakeResolver(fake.dir)};
    fs::path                   project = fake.dir / "project";

    Fixture() {
        fs::create_directories(project / ".git");
        fs::create_directories(project / "src");
        std::ofstream(project / "src" / "a.c");
        project = fs::canonical(project);
    }
};

struct Run {
    std::optional<remote::Spawned>        spawned;
    std::string                           output;
    std::optional<std::optional<int>>     exit;
    std::optional<process::StreamedChild> child;

    auto Done() {
        return [this](remote::Spawned result) { spawned = std::move(result); };
    }
    // Streams the spawned child to its end.
    bool Finish(ned::editor::PostQueue& queue) {
        if (!PumpUntil(queue, [this] { return spawned.has_value(); }) || !spawned->has_value()) {
            return false;
        }
        child.emplace(std::move(**spawned), queue, [this](std::string_view chunk) { output += chunk; }, [this](std::optional<int> code) { exit = code; });
        return PumpUntil(queue, [this] { return exit.has_value(); });
    }
};

remote::RemoteSpawn Pwd() {
    return remote::RemoteSpawn{.argv = {"pwd"}, .cwd = std::nullopt, .env = {}, .stderrMode = process::StderrMode::MergeWithStdout};
}

} // namespace

TEST_CASE("A local buffer's process starts here, at once", "[Remote][RemotePlacement]") {
    ned::editor::LifetimeGuard guard;
    ned::ui::EventLoop         loop;
    remote::RemoteSpawn        spawn = Pwd();
    spawn.cwd                        = fs::temp_directory_path();
    Run run;
    remote::SpawnBeside(nullptr, std::nullopt, spawn, guard.Token(), run.Done());
    REQUIRE(run.spawned.has_value());
    REQUIRE(run.Finish(loop));
    CHECK(run.output == fs::canonical(fs::temp_directory_path()).string() + "\n");

    Run missing;
    spawn.argv = {"ned-placement-test-no-such-command"};
    remote::SpawnBeside(nullptr, std::nullopt, spawn, guard.Token(), missing.Done());
    REQUIRE(missing.spawned.has_value());
    CHECK_FALSE(missing.spawned->has_value());

    Run nowhere;
    remote::SpawnBeside(nullptr, ned::text::RemoteFile{.destination = "host", .path = "/a"}, Pwd(), guard.Token(), nowhere.Done());
    REQUIRE(nowhere.spawned.has_value());
    CHECK_THAT(nowhere.spawned->error(), ContainsSubstring("no remote host"));
}

TEST_CASE("A remote buffer's process runs on its host, in its project root", "[Remote][RemotePlacement][HostAgent]") {
    Fixture                     fixture;
    ned::editor::LifetimeGuard  guard;
    const ned::text::RemoteFile file{.destination = "host", .path = fixture.project / "src" / "a.c"};

    // Connecting first, since nothing has yet.
    Run rooted;
    remote::SpawnBeside(&fixture.hosts, file, Pwd(), guard.Token(), rooted.Done());
    REQUIRE(rooted.Finish(fixture.loop));
    CHECK(rooted.output == fixture.project.string() + "\n");
    CHECK(rooted.exit == std::optional<std::optional<int>>(0));

    Run                 given;
    remote::RemoteSpawn spawn = Pwd();
    spawn.cwd                 = fixture.project / "src";
    remote::SpawnBeside(&fixture.hosts, file, spawn, guard.Token(), given.Done());
    REQUIRE(given.Finish(fixture.loop));
    CHECK(given.output == (fixture.project / "src").string() + "\n");

    SECTION("not once its owner has gone") {
        Run orphan;
        {
            ned::editor::LifetimeGuard gone;
            REQUIRE(fixture.hosts.Disconnect("host"));
            remote::SpawnBeside(&fixture.hosts, file, Pwd(), gone.Token(), orphan.Done());
        }
        Run after;
        remote::SpawnBeside(&fixture.hosts, file, Pwd(), guard.Token(), after.Done());
        REQUIRE(after.Finish(fixture.loop));
        CHECK_FALSE(orphan.spawned.has_value());
    }
}

TEST_CASE("A task started from a remote buffer runs on its host, and again from its output", "[Remote][RemotePlacement][Tasks][HostAgent]") {
    Fixture               fixture;
    ned::text::BufferList buffers;
    tasks::TaskRunner     runner(buffers, fixture.loop);
    runner.SetHosts(&fixture.hosts);
    tasks::SetTaskCommand("remote-placement-test-pwd", {"pwd"});

    ned::text::Buffer& source = buffers.CreateBuffer("a.c");
    source.SetRemote(ned::text::RemoteFile{.destination = "host", .path = fixture.project / "src" / "a.c"});

    ned::text::Buffer* output = runner.RunTask("remote-placement-test-pwd", &source);
    REQUIRE(output != nullptr);
    CHECK(runner.IsRunning("remote-placement-test-pwd"));
    REQUIRE(PumpUntil(fixture.loop, [&] { return !runner.IsRunning("remote-placement-test-pwd"); }));
    CHECK_THAT(output->Text(), ContainsSubstring(fixture.project.string() + "\n\n[exited 0]"));

    (void)runner.RunTask("remote-placement-test-pwd", output);
    REQUIRE(PumpUntil(fixture.loop, [&] { return !runner.IsRunning("remote-placement-test-pwd"); }));
    CHECK_THAT(output->Text(), ContainsSubstring("--- re-run ---\n" + fixture.project.string() + "\n"));

    SECTION("cancelled while it connects") {
        REQUIRE(fixture.hosts.Disconnect("host"));
        (void)runner.RunTask("remote-placement-test-pwd", &source);
        REQUIRE(runner.IsRunning("remote-placement-test-pwd"));
        runner.CancelTask("remote-placement-test-pwd");
        CHECK_FALSE(runner.IsRunning("remote-placement-test-pwd"));
        CHECK(output->Text().ends_with("[cancelled]\n"));
        // The connection it asked for still lands, and nothing runs on it.
        REQUIRE(PumpUntil(fixture.loop, [&] { return fixture.hosts.Find("host") != nullptr; }));
    }

    SECTION("an unreachable host says why") {
        source.SetRemote(ned::text::RemoteFile{.destination = "denied", .path = "/a.c"});
        (void)runner.RunTask("remote-placement-test-pwd", &source);
        REQUIRE(PumpUntil(fixture.loop, [&] { return !runner.IsRunning("remote-placement-test-pwd"); }));
        CHECK_THAT(output->Text(), ContainsSubstring("Failed to start task \"remote-placement-test-pwd\""));
    }
    tasks::SetTaskCommand("remote-placement-test-pwd", {});
}
