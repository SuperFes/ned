//
// The format command run beside a buffer without waiting for it: here for a
// local buffer, and for a remote one on its host, in its project root
// there, against ned_fake_ssh and the real agent.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatRun.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteHost.h"
#include "UI/EventLoop.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::FormatRun;
using ned::tests::PumpUntil;
namespace fs     = std::filesystem;
namespace remote = ned::editor::remote;

namespace {

using Formatted = std::expected<std::string, std::string>;

struct Outcomes {
    std::vector<Formatted> seen;

    auto Done() {
        return [this](Formatted formatted) { seen.push_back(std::move(formatted)); };
    }
};

struct Fixture {
    ned::tests::FakeRemoteHost fake;
    ned::ui::EventLoop         loop;
    remote::Hosts              hosts{loop, [this](const std::string& destination) { return fake.Options(destination); }, ned::tests::FakeResolver(fake.dir)};
    fs::path                   project = fake.dir / "project";

    Fixture() {
        fs::create_directories(project / ".git");
        fs::create_directories(project / "src");
        project = fs::canonical(project);
    }

    [[nodiscard]] ned::text::RemoteFile File() const {
        return ned::text::RemoteFile{.destination = "host", .path = project / "src" / "a.c"};
    }
};

} // namespace

TEST_CASE("A local buffer's format runs here and answers later", "[FormatRun]") {
    ned::ui::EventLoop loop;
    FormatRun          run(loop);
    Outcomes           outcomes;

    run.Start(nullptr, std::nullopt, "tr a-z A-Z", "one\ntwo\n", outcomes.Done());
    CHECK(outcomes.seen.empty());
    CHECK(run.Running());
    REQUIRE(PumpUntil(loop, [&] { return !outcomes.seen.empty(); }));
    REQUIRE(outcomes.seen.size() == 1);
    CHECK(outcomes.seen.front() == Formatted("ONE\nTWO\n"));
    CHECK_FALSE(run.Running());

    SECTION("a command that fails formats nothing") {
        run.Start(nullptr, std::nullopt, "cat; exit 1", "text", outcomes.Done());
        REQUIRE(PumpUntil(loop, [&] { return outcomes.seen.size() == 2; }));
        CHECK_FALSE(outcomes.seen.back().has_value());
    }
    SECTION("input bigger than a pipe goes in while output comes out") {
        const std::string big(4 * 1024 * 1024, 'x');
        run.Start(nullptr, std::nullopt, "cat", big, outcomes.Done());
        REQUIRE(PumpUntil(loop, [&] { return outcomes.seen.size() == 2; }));
        CHECK(outcomes.seen.back() == Formatted(big));
    }
}

TEST_CASE("Starting a format abandons the one before, and so does destroying it", "[FormatRun]") {
    ned::ui::EventLoop loop;
    Outcomes           outcomes;
    {
        FormatRun run(loop);
        run.Start(nullptr, std::nullopt, "sleep 30; cat", "first", outcomes.Done());
        run.Start(nullptr, std::nullopt, "cat", "second", outcomes.Done());
        REQUIRE(PumpUntil(loop, [&] { return !outcomes.seen.empty(); }));
        CHECK(outcomes.seen == std::vector<Formatted>{Formatted("second")});

        run.Start(nullptr, std::nullopt, "sleep 30; cat", "third", outcomes.Done());
        run.Cancel();
        CHECK_FALSE(run.Running());
        run.Start(nullptr, std::nullopt, "sleep 30; cat", "fourth", outcomes.Done());
    }
    loop.DrainPosted_();
    CHECK(outcomes.seen.size() == 1);
}

TEST_CASE("A remote buffer's format runs on its host, in its project root", "[FormatRun][Remote][HostAgent]") {
    Fixture   fixture;
    FormatRun run(fixture.loop);
    Outcomes  outcomes;

    run.Start(&fixture.hosts, fixture.File(), "pwd; tr a-z A-Z", "text\n", outcomes.Done());
    REQUIRE(PumpUntil(fixture.loop, [&] { return !outcomes.seen.empty(); }));
    CHECK(outcomes.seen.front() == Formatted(fixture.project.string() + "\nTEXT\n"));

    SECTION("with input bigger than any socket buffer") {
        std::string big;
        for (int line = 0; big.size() < 4 * 1024 * 1024; ++line) {
            big += "line " + std::to_string(line) + "\n";
        }
        run.Start(&fixture.hosts, fixture.File(), "cat", big, outcomes.Done());
        REQUIRE(PumpUntil(fixture.loop, [&] { return outcomes.seen.size() == 2; }));
        CHECK(outcomes.seen.back() == Formatted(big));
    }
    SECTION("a host it can't reach says why") {
        run.Start(&fixture.hosts, ned::text::RemoteFile{.destination = "denied", .path = "/a.c"}, "cat", "text", outcomes.Done());
        REQUIRE(PumpUntil(fixture.loop, [&] { return outcomes.seen.size() == 2; }));
        REQUIRE_FALSE(outcomes.seen.back().has_value());
        CHECK_FALSE(outcomes.seen.back().error().empty());
    }
}
