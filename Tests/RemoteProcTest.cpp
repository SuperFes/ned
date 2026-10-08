//
// A host's processes through the relay, against ned_fake_ssh and the real
// agent, read the way a local child is: through StreamedChild.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <thread>

#include "Editor/PostQueue.h"
#include "Editor/Process/StreamedChild.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/Proc.h"
#include "FakeRemoteHost.h"

using Catch::Matchers::ContainsSubstring;
using ned::tests::PumpUntil;
namespace process = ned::editor::process;
namespace remote  = ned::editor::remote;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost fake;
    ned::editor::PostQueue     queue;
    remote::Hosts              hosts{queue, [this](const std::string& destination) { return fake.Options(destination); },
                                     ned::tests::FakeResolver(fake.dir)};
    remote::Connection*        connection = nullptr;

    Fixture() {
        hosts.Connect("host", [this](const std::expected<remote::Connection*, std::string>& connected) {
            REQUIRE(connected.has_value());
            connection = *connected;
        });
        REQUIRE(PumpUntil(queue, [this] { return connection != nullptr; }));
    }
};

// A child's output and exit as StreamedChild hands them over.
struct Streamed {
    std::string                           output;
    std::optional<std::optional<int>>     exit;
    std::optional<process::StreamedChild> child;

    void Start(process::ChildProcess spawned, ned::editor::PostQueue& queue) {
        child.emplace(std::move(spawned), queue, [this](std::string_view chunk) { output += chunk; }, [this](std::optional<int> code) { exit = code; });
    }
};

remote::RemoteSpawn Spawn(std::vector<std::string> argv, process::StderrMode stderrMode = process::StderrMode::MergeWithStdout) {
    return remote::RemoteSpawn{.argv = std::move(argv), .cwd = std::nullopt, .env = {}, .stderrMode = stderrMode};
}

} // namespace

TEST_CASE("A host's process reads and writes like a local child", "[Remote][RemoteProc][HostAgent]") {
    Fixture  fixture;
    Streamed cat;
    cat.Start(remote::SpawnOnHost(*fixture.connection, Spawn({"sh", "-c", "cat; echo err >&2; exit 4"})), fixture.queue);
    cat.child->Child().WriteAll("hello\n", std::chrono::seconds(5));
    cat.child->Child().CloseInput();
    REQUIRE(PumpUntil(fixture.queue, [&] { return cat.exit.has_value(); }));
    CHECK(cat.output == "hello\nerr\n");
    CHECK(cat.exit == std::optional<std::optional<int>>(4));

    SECTION("in the host's directory and environment") {
        const std::filesystem::path dir   = std::filesystem::canonical(fixture.fake.dir);
        remote::RemoteSpawn         spawn = Spawn({"sh", "-c", "pwd; echo \"$NED_REMOTE_PROC\""});
        spawn.cwd                         = dir;
        spawn.env                         = {{"NED_REMOTE_PROC", "there"}};
        Streamed env;
        env.Start(remote::SpawnOnHost(*fixture.connection, std::move(spawn)), fixture.queue);
        REQUIRE(PumpUntil(fixture.queue, [&] { return env.exit.has_value(); }));
        CHECK(env.output == dir.string() + "\nthere\n");
    }
    SECTION("a flood arrives whole") {
        Streamed flood;
        flood.Start(remote::SpawnOnHost(*fixture.connection, Spawn({"head", "-c", "20000000", "/dev/zero"})), fixture.queue);
        REQUIRE(PumpUntil(fixture.queue, [&] { return flood.exit.has_value(); }));
        CHECK(flood.output.size() == 20000000);
        CHECK(flood.exit == std::optional<std::optional<int>>(0));
    }
    SECTION("input bigger than any socket buffer goes in while output comes out") {
        std::string input(4 * 1024 * 1024, 'x');
        for (std::size_t i = 0; i < input.size(); i += 4096) {
            input[i] = static_cast<char>('a' + i / 4096 % 26);
        }
        Streamed echo;
        echo.Start(remote::SpawnOnHost(*fixture.connection, Spawn({"cat"})), fixture.queue);
        echo.child->Feed(input, std::chrono::seconds(5));
        REQUIRE(PumpUntil(fixture.queue, [&] { return echo.exit.has_value(); }));
        CHECK(echo.output == input);
        CHECK(echo.exit == std::optional<std::optional<int>>(0));
    }
    SECTION("Kill ends it without waiting on the host") {
        Streamed sleeper;
        sleeper.Start(remote::SpawnOnHost(*fixture.connection, Spawn({"sleep", "100"})), fixture.queue);
        const auto before = std::chrono::steady_clock::now();
        sleeper.child->Child().Kill();
        CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100));
        REQUIRE(PumpUntil(fixture.queue, [&] { return sleeper.exit.has_value(); }));
        CHECK(sleeper.exit == std::optional<std::optional<int>>(std::optional<int>()));
    }
    SECTION("dropping it returns at once") {
        process::ChildProcess dropped = remote::SpawnOnHost(*fixture.connection, Spawn({"sleep", "100"}));
        const auto            before  = std::chrono::steady_clock::now();
        {
            process::ChildProcess gone = std::move(dropped);
        }
        CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100));
        // The host still answers: the release didn't wedge the connection.
        Streamed after;
        after.Start(remote::SpawnOnHost(*fixture.connection, Spawn({"echo", "after"})), fixture.queue);
        REQUIRE(PumpUntil(fixture.queue, [&] { return after.exit.has_value(); }));
        CHECK(after.output == "after\n");
    }
}

TEST_CASE("A host's process that can't start says why, and one whose host goes ends", "[Remote][RemoteProc][HostAgent]") {
    Fixture  fixture;
    Streamed missing;
    missing.Start(remote::SpawnOnHost(*fixture.connection, Spawn({"ned-no-such-command"})), fixture.queue);
    REQUIRE(PumpUntil(fixture.queue, [&] { return missing.exit.has_value(); }));
    CHECK_THAT(missing.output, ContainsSubstring("ned-no-such-command"));
    CHECK(missing.exit == std::optional<std::optional<int>>(remote::kRemoteSpawnFailed));

    // Captured, stderr has its own descriptor.
    process::ChildProcess captured = remote::SpawnOnHost(*fixture.connection, Spawn({"sh", "-c", "echo out; echo err >&2"}, process::StderrMode::Capture));
    std::string           err;
    std::jthread          reader([&] {
        for (std::string chunk; !(chunk = captured.ReadSomeStderr()).empty();) {
            err += chunk;
        }
    });
    std::string           out;
    REQUIRE(PumpUntil(fixture.queue, [&] {
        while (const std::optional<std::string> chunk = captured.ReadSome(std::chrono::milliseconds(0))) {
            if (chunk->empty()) {
                return true;
            }
            out += *chunk;
        }
        return false;
    }));
    reader.join();
    CHECK(out == "out\n");
    CHECK(err == "err\n");

    Streamed sleeper;
    sleeper.Start(remote::SpawnOnHost(*fixture.connection, Spawn({"sleep", "100"})), fixture.queue);
    REQUIRE(fixture.hosts.Disconnect("host"));
    REQUIRE(PumpUntil(fixture.queue, [&] { return sleeper.exit.has_value(); }));
    CHECK(sleeper.exit == std::optional<std::optional<int>>(std::optional<int>()));
}
