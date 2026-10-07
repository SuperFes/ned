//
// A remote buffer's repository, found and worked in on its host, against
// ned_fake_ssh, the real agent and the bundled git plugin.
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
#include "Editor/Process/ChildProcess.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Vcs/HostRepo.h"
#include "Editor/Vcs/ProviderRegistry.h"
#include "Editor/Vcs/Runner.h"
#include "FakeRemoteHost.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/PluginLoader.h"
#include "JanetTestSupport.h"
#include "Text/Buffer.h"
#include "UI/EventLoop.h"

using Catch::Matchers::ContainsSubstring;
using ned::tests::PumpUntil;
namespace fs     = std::filesystem;
namespace remote = ned::editor::remote;
namespace vcs    = ned::editor::vcs;

namespace {

bool GitAvailable() {
    try {
        ned::editor::process::ChildProcess probe({"git", "--version"});
        (void)probe.WaitForExit();
        return true;
    }
    catch (const std::runtime_error&) {
        return false;
    }
}

std::string Run(const std::vector<std::string>& argv) {
    ned::editor::process::ChildProcess process(argv, ned::editor::process::StderrMode::MergeWithStdout);
    std::string                        output;
    for (std::string chunk = process.ReadSome(); !chunk.empty(); chunk = process.ReadSome()) {
        output += chunk;
    }
    const std::optional<int> exit = process.WaitForExit();
    REQUIRE(exit == std::optional<int>(0));
    return output;
}

struct RegistryReset {
    RegistryReset() {
        vcs::ClearRegistry();
    }
    ~RegistryReset() {
        vcs::ClearRegistry();
    }
};

// A git repository on the host, at repo, with file.txt committed and then
// changed on lines 2 and 7.
struct Fixture {
    RegistryReset              registry;
    ned::tests::FakeRemoteHost fake;
    ned::ui::EventLoop         loop;
    remote::Hosts              hosts{loop, [this](const std::string& destination) { return fake.Options(destination); }, ned::tests::FakeResolver(fake.dir)};
    fs::path                   repo = fake.dir / "repo";
    fs::path                   file;

    Fixture() {
        ned::janet::Environment& env = ned_tests::TestEnvironment();
        ned::janet::InstallEditorBindings(env);
        ned::janet::LoadBundledPlugins(env);

        fs::create_directories(repo / "src");
        repo = fs::canonical(repo);
        file = repo / "file.txt";
        Run({"git", "-C", repo.string(), "init", "-q", "-b", "main"});
        Run({"git", "-C", repo.string(), "config", "user.email", "ned-test@example.com"});
        Run({"git", "-C", repo.string(), "config", "user.name", "Ned Test"});
        std::ofstream(file) << "line one\nline two\nline three\nline four\nline five\nline six\nline seven\n";
        Run({"git", "-C", repo.string(), "add", "file.txt"});
        Run({"git", "-C", repo.string(), "commit", "-q", "-m", "initial commit"});
        std::ofstream(file) << "line one\nline TWO\nline three\nline four\nline five\nline six\nline SEVEN\n";
    }

    [[nodiscard]] std::string Staged() const {
        return Run({"git", "-C", repo.string(), "diff", "--cached", "--no-color"});
    }
};

template <typename T>
struct Outcome {
    std::optional<T>           value;
    std::optional<std::string> error;

    [[nodiscard]] bool Settled() const {
        return value.has_value() || error.has_value();
    }
    auto OnValue() {
        return [this](T result) { value = std::move(result); };
    }
    auto OnError() {
        return [this](std::string why) { error = std::move(why); };
    }
};

} // namespace

TEST_CASE("A remote file's repository and provider are found on its host", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture fixture;
    ned_tests::TestEnvironment().DoString(R"((ned/vcs-register-provider "unmarked" {:detect (fn [root] true)}))");
    ned::editor::LifetimeGuard guard;

    std::optional<std::expected<vcs::Repo, std::string>> repo;
    vcs::FindRepoOnHost(fixture.hosts, ned::text::RemoteFile{.destination = "host", .path = fixture.repo / "src" / "new.c"}, guard.Token(),
                        [&repo](std::expected<vcs::Repo, std::string> found) { repo = std::move(found); });
    REQUIRE(PumpUntil(fixture.loop, [&repo] { return repo.has_value(); }));
    REQUIRE(repo->has_value());
    CHECK(**repo == vcs::Repo{.destination = "host", .root = fixture.repo});

    // The unmarked provider would claim anything here, but can't be found there.
    std::optional<std::expected<vcs::Provider*, std::string>> provider;
    vcs::FindProviderOnHost(fixture.hosts, **repo, guard.Token(), [&provider](std::expected<vcs::Provider*, std::string> found) { provider = found; });
    REQUIRE(PumpUntil(fixture.loop, [&provider] { return provider.has_value(); }));
    REQUIRE(provider->has_value());
    CHECK(**provider == vcs::ProviderNamed("git"));

    SECTION("found once, it answers at once") {
        std::optional<std::expected<vcs::Provider*, std::string>> again;
        vcs::FindProviderOnHost(fixture.hosts, **repo, guard.Token(), [&again](std::expected<vcs::Provider*, std::string> found) { again = found; });
        REQUIRE(again.has_value());
        CHECK(*again == *provider);

        vcs::ClearProviderCache();
        CHECK_FALSE(vcs::CachedHostProvider("host", fixture.repo).has_value());
    }

    SECTION("a directory without a marker has none") {
        std::optional<std::expected<vcs::Provider*, std::string>> none;
        vcs::FindProviderOnHost(fixture.hosts, vcs::Repo{.destination = "host", .root = fixture.repo / "src"}, guard.Token(),
                                [&none](std::expected<vcs::Provider*, std::string> found) { none = found; });
        REQUIRE(PumpUntil(fixture.loop, [&none] { return none.has_value(); }));
        REQUIRE(none->has_value());
        CHECK(**none == nullptr);
    }

    SECTION("an unreachable host says so") {
        std::optional<std::expected<vcs::Repo, std::string>> denied;
        vcs::FindRepoOnHost(fixture.hosts, ned::text::RemoteFile{.destination = "denied", .path = "/a"}, guard.Token(),
                            [&denied](std::expected<vcs::Repo, std::string> found) { denied = std::move(found); });
        REQUIRE(PumpUntil(fixture.loop, [&denied] { return denied.has_value(); }));
        CHECK_FALSE(denied->has_value());
    }
}

TEST_CASE("A remote buffer's diff, blame and hunks run in its repository on its host", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture     fixture;
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);
    const ned::text::Buffer buffer = ned::text::Buffer::NewRemoteFile(ned::text::RemoteFile{.destination = "host", .path = fixture.file});

    Outcome<std::vector<vcs::DiffHunk>> diff;
    runner.RequestDiff(buffer, diff.OnValue(), diff.OnError());
    REQUIRE(PumpUntil(fixture.loop, [&diff] { return diff.Settled(); }));
    REQUIRE(diff.value.has_value());
    CHECK(diff.value->size() == 2);

    Outcome<std::vector<vcs::BlameLine>> blame;
    runner.RequestBlame(buffer, blame.OnValue(), blame.OnError());
    REQUIRE(PumpUntil(fixture.loop, [&blame] { return blame.Settled(); }));
    REQUIRE(blame.value.has_value());
    CHECK(blame.value->size() == 7);

    // The patch reaches the host on stdin.
    Outcome<bool> staged;
    runner.RequestHunkApply(buffer, 2, /*stage=*/true, [&staged] { staged.value = true; }, staged.OnError());
    REQUIRE(PumpUntil(fixture.loop, [&staged] { return staged.Settled(); }));
    REQUIRE(staged.value.has_value());
    CHECK_THAT(fixture.Staged(), ContainsSubstring("+line TWO"));
    CHECK_FALSE(fixture.Staged().contains("SEVEN"));

    const vcs::Repo                        repo{.destination = "host", .root = fixture.repo};
    Outcome<std::vector<vcs::StatusEntry>> status;
    runner.RequestStatus(repo, status.OnValue(), status.OnError());
    REQUIRE(PumpUntil(fixture.loop, [&status] { return status.Settled(); }));
    REQUIRE(status.value.has_value());
    REQUIRE(status.value->size() == 1);
    CHECK((*status.value)[0].state == "MM");

    Outcome<bool> unstaged;
    runner.RequestHunkApply(buffer, 2, /*stage=*/false, [&unstaged] { unstaged.value = true; }, unstaged.OnError());
    REQUIRE(PumpUntil(fixture.loop, [&unstaged] { return unstaged.Settled(); }));
    REQUIRE(unstaged.value.has_value());
    CHECK(fixture.Staged().empty());
}

TEST_CASE("A remote buffer's repository needs a host to reach", "[Remote][RemoteVcs]") {
    RegistryReset           registry;
    ned::ui::EventLoop      loop;
    vcs::Runner             runner(loop);
    const ned::text::Buffer buffer = ned::text::Buffer::NewRemoteFile(ned::text::RemoteFile{.destination = "host", .path = "/src/a.c"});

    Outcome<std::vector<vcs::DiffHunk>> diff;
    runner.RequestDiff(buffer, diff.OnValue(), diff.OnError());
    REQUIRE(diff.error.has_value());
    CHECK_THAT(*diff.error, ContainsSubstring("no remote host"));

    Outcome<std::vector<vcs::StatusEntry>> status;
    runner.RequestStatus(vcs::Repo{.destination = "host", .root = "/src"}, status.OnValue(), status.OnError());
    REQUIRE(status.error.has_value());
    CHECK_THAT(*status.error, ContainsSubstring("no remote host"));
}
