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
#include <set>
#include <string>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/ConflictResolution.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lifetime.h"
#include "Editor/Mode.h"
#include "Editor/Process/ChildProcess.h"
#include "Editor/Project/Root.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Tracker/Registry.h"
#include "Editor/Vcs/CurrentBranch.h"
#include "Editor/Vcs/HostRepo.h"
#include "Editor/Vcs/ProviderRegistry.h"
#include "Editor/Vcs/Runner.h"
#include "Editor/Vcs/Sequence.h"
#include "FakeRemoteHost.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/PluginLoader.h"
#include "JanetTestSupport.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/RemotePanel.h"
#include "UI/Theme.h"
#include "UI/VcsPanel.h"

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

// Leaves file.txt conflicted by merging a branch that changed its first line
// against main's change to it; other.txt is committed beside it, clean.
void MergeConflict(const fs::path& repo) {
    const auto write = [&](const std::string& first) {
        std::ofstream(repo / "file.txt") << first << "\nline two\nline three\n";
    };
    std::ofstream(repo / "other.txt") << "other\n";
    write("line one");
    Run({"git", "-C", repo.string(), "add", "file.txt", "other.txt"});
    Run({"git", "-C", repo.string(), "commit", "-q", "-m", "base"});
    Run({"git", "-C", repo.string(), "checkout", "-q", "-b", "other"});
    write("line one OTHER");
    Run({"git", "-C", repo.string(), "commit", "-q", "-am", "other"});
    Run({"git", "-C", repo.string(), "checkout", "-q", "main"});
    write("line one MAIN");
    Run({"git", "-C", repo.string(), "commit", "-q", "-am", "main"});
    ned::editor::process::ChildProcess merge({"git", "-C", repo.string(), "merge", "-q", "other"}, ned::editor::process::StderrMode::MergeWithStdout);
    while (!merge.ReadSome().empty()) {
    }
    REQUIRE(merge.WaitForExit() == std::optional<int>(1));
}

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

TEST_CASE("A buffer's repository is its file's, or the last one found", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture     fixture;
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);
    const fs::path previousRoot = ned::editor::ProjectRoot();
    ned::editor::SetProjectRoot("/src/local");

    const auto repoOf = [&](const ned::text::Buffer& buffer) {
        std::optional<std::expected<vcs::Repo, std::string>> repo;
        runner.RepoFor(buffer, [&repo](std::expected<vcs::Repo, std::string> found) { repo = std::move(found); });
        REQUIRE(PumpUntil(fixture.loop, [&repo] { return repo.has_value(); }));
        REQUIRE(repo->has_value());
        return **repo;
    };
    const vcs::Repo         onHost{.destination = "host", .root = fixture.repo};
    const ned::text::Buffer remoteBuffer = ned::text::Buffer::NewRemoteFile(ned::text::RemoteFile{.destination = "host", .path = fixture.file});
    const ned::text::Buffer localBuffer  = ned::text::Buffer::NewFile("/src/local/a.c");
    const ned::text::Buffer scratch("*vcs status*");
    const ned::text::Buffer commitMessage = ned::text::Buffer::NewFile(vcs::CommitMessagePath());

    CHECK(repoOf(scratch) == vcs::LocalRepo());
    CHECK(repoOf(remoteBuffer) == onHost);
    CHECK(repoOf(scratch) == onHost);
    CHECK(repoOf(commitMessage) == onHost);

    // Found once, a remote file's repository answers at once.
    bool answered = false;
    runner.RepoFor(remoteBuffer, [&answered](std::expected<vcs::Repo, std::string>) { answered = true; });
    CHECK(answered);

    CHECK(repoOf(localBuffer) == vcs::LocalRepo());
    CHECK(repoOf(scratch) == vcs::LocalRepo());
    ned::editor::SetProjectRoot(previousRoot);
}

TEST_CASE("A commit begun in a remote buffer commits on its host", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture     fixture;
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);

    ned::text::BufferList bufferList;
    ned::text::Buffer&    remoteBuffer = bufferList.AddRemotePlaceholder(ned::text::RemoteFile{.destination = "host", .path = fixture.file});
    remoteBuffer.FinishLoad(ned::text::Rope("line one\nline TWO\nline three\nline four\nline five\nline six\nline SEVEN\n"));
    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::editor::CommandRegistry registry = [] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }();
    ned::editor::Keymap     keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode       mode  = ned::editor::FundamentalMode();
    ned::ui::Theme          theme = ned::ui::DarkTheme();
    std::string             statusMessage;
    ned::ui::ActiveBuffer   activeBuffer{remoteBuffer};
    ned::ui::BufferView     view(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    view.SetVcsRunner(&runner);

    remoteBuffer.SetPoint(remoteBuffer.ByteOffsetForLineAndColumn(1, 0));
    view.StageHunkAtPointForTesting(true);
    REQUIRE(PumpUntil(fixture.loop, [&statusMessage] { return statusMessage == "Hunk staged." || statusMessage.contains("vcs"); }));
    REQUIRE(statusMessage == "Hunk staged.");

    view.BeginVcsCommitMessageForTesting();
    REQUIRE(activeBuffer.Get().Path() == vcs::CommitMessagePath());
    activeBuffer.Get().InsertAtPoint("Stage the second line");
    view.FinishVcsCommitMessageForTesting();
    REQUIRE(PumpUntil(fixture.loop, [&statusMessage] { return statusMessage != "Committing..."; }));
    CHECK_THAT(statusMessage, ContainsSubstring("Stage the second line"));
    CHECK_THAT(Run({"git", "-C", fixture.repo.string(), "log", "-1", "--pretty=%s"}), ContainsSubstring("Stage the second line"));
    CHECK(fixture.Staged().empty());
}

TEST_CASE("The VCS panel shows the active remote buffer's repository", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture     fixture;
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);

    ned::text::BufferList list;
    ned::text::Buffer&    scratch = list.CreateBuffer("scratch");
    ned::text::Buffer&    remote  = list.AddRemotePlaceholder(ned::text::RemoteFile{.destination = "host", .path = fixture.file});
    ned::ui::ActiveBuffer activeBuffer(remote);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel     panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    panel.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 19});
    panel.SetVcsRunner(&runner);
    std::optional<ned::text::RemoteFile> opened;
    panel.SetOnOpenRemoteFile([&opened](const ned::text::RemoteFile& file) { opened = file; });

    ned::ui::Screen screen(40, 20);
    const auto      shows = [&](const std::string& text) {
        panel.Paint(ned::ui::Canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 19}));
        for (int row = 0; row < 20; ++row) {
            std::string line;
            for (int col = 0; col < 40; ++col) {
                line += screen.PixelAt(col, row).character;
            }
            if (line.contains(text)) {
                return true;
            }
        }
        return false;
    };
    REQUIRE(PumpUntil(fixture.loop, [&] { return shows("file.txt"); }));
    CHECK(panel.Repository() == vcs::Repo{.destination = "host", .root = fixture.repo});

    // Its rows are the host's files, opened there.
    panel.OpenFileEntry(fixture.file);
    CHECK(activeBuffer.Get().Remote() == ned::text::RemoteFile{.destination = "host", .path = fixture.file});
    panel.OpenFileEntry(fixture.repo / "elsewhere.txt");
    CHECK(opened == ned::text::RemoteFile{.destination = "host", .path = fixture.repo / "elsewhere.txt"});

    // A local buffer switches it back, dropping what the host showed.
    const fs::path previousRoot = ned::editor::ProjectRoot();
    ned::editor::SetProjectRoot(fixture.fake.dir / "nowhere");
    ned::text::Buffer& local = list.OpenOrCreateFile(fixture.fake.dir / "nowhere" / "a.txt");
    activeBuffer.Set(local);
    panel.ForceRefresh();
    CHECK(panel.Repository() == vcs::LocalRepo());
    CHECK_FALSE(shows("file.txt"));
    ned::editor::SetProjectRoot(previousRoot);
    (void)scratch;
}

TEST_CASE("A remote buffer's mode line issue key reads its repository's branch on its host", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture fixture;
    Run({"git", "-C", fixture.repo.string(), "checkout", "-q", "-b", "feature/DEV-450-login"});
    ned::editor::tracker::ClearRegistry();
    ned::editor::tracker::SetConnection(ned::editor::tracker::Connection{.name = "work", .provider = "jira"});
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);

    ned::text::BufferList bufferList;
    ned::text::Buffer&    remoteBuffer = bufferList.AddRemotePlaceholder(ned::text::RemoteFile{.destination = "host", .path = fixture.file});
    remoteBuffer.FinishLoad(ned::text::Rope("line one\n"));
    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::editor::CommandRegistry registry;
    ned::editor::Keymap          keymap;
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();
    std::string                  statusMessage;
    ned::ui::ActiveBuffer        activeBuffer{remoteBuffer};
    ned::ui::BufferView          view(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    view.SetVcsRunner(&runner);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    CHECK_FALSE(runner.KnownRepoFor(remoteBuffer));
    CHECK_FALSE(view.IssueKey()); // not this machine's branch, nor yet the host's

    REQUIRE(PumpUntil(fixture.loop, [&] { return view.IssueKey().has_value(); }));
    CHECK(view.IssueKey() == "DEV-450");
    CHECK(runner.KnownRepoFor(remoteBuffer) == vcs::Repo{.destination = "host", .root = fixture.repo});

    // A local buffer reads this machine's branch instead.
    ned::editor::vcs::SetCurrentBranch(vcs::LocalRepo(), "main");
    ned::text::Buffer& local = bufferList.CreateBuffer("local.txt");
    local.SetPath(fixture.fake.dir / "local.txt");
    activeBuffer.Set(local);
    CHECK_FALSE(view.IssueKey());
    activeBuffer.Set(remoteBuffer);
    CHECK(view.IssueKey() == "DEV-450");

    ned::editor::tracker::ClearRegistry();
    ned::editor::vcs::ClearCurrentBranches();
}

TEST_CASE("A host's conflicted files are read there, an open buffer's text first", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture fixture;
    MergeConflict(fixture.repo);
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);
    const vcs::Repo                   host{.destination = "host", .root = fixture.repo};
    const std::vector<fs::path>       paths = {fixture.file, fixture.repo / "other.txt", fixture.repo / "gone.txt"};
    std::optional<std::set<fs::path>> marked;
    const auto                        record = [&marked](std::set<fs::path> found) { marked = std::move(found); };

    runner.RequestMarkedFiles(host, paths, nullptr, record);
    REQUIRE(PumpUntil(fixture.loop, [&marked] { return marked.has_value(); }));
    CHECK(*marked == std::set<fs::path>{fixture.file});

    // An open buffer's text outranks the file.
    marked.reset();
    runner.RequestMarkedFiles(host, paths, [&](const fs::path& path) { return path == fixture.file ? std::optional(false) : std::nullopt; }, record);
    REQUIRE(PumpUntil(fixture.loop, [&marked] { return marked.has_value(); }));
    CHECK(marked->empty());

    // This machine's are read at once.
    marked.reset();
    runner.RequestMarkedFiles(vcs::Repo{.destination = std::nullopt, .root = fixture.repo}, paths, nullptr, record);
    CHECK(marked == std::set<fs::path>{fixture.file});

    // A host that can't be asked leaves every file marked.
    vcs::Runner unreachable(fixture.loop);
    marked.reset();
    unreachable.RequestMarkedFiles(host, paths, nullptr, record);
    CHECK(marked == std::set<fs::path>(paths.begin(), paths.end()));
}

TEST_CASE("Continuing a host's merge reads its unopened files there", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture fixture;
    MergeConflict(fixture.repo);
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);

    ned::text::BufferList bufferList;
    ned::text::Buffer&    other = bufferList.AddRemotePlaceholder(ned::text::RemoteFile{.destination = "host", .path = fixture.repo / "other.txt"});
    other.FinishLoad(ned::text::Rope("other\n"));
    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::editor::CommandRegistry registry;
    ned::editor::Keymap          keymap;
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();
    std::string                  statusMessage;
    ned::ui::ActiveBuffer        activeBuffer{other};
    ned::ui::BufferView          view(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    view.SetVcsRunner(&runner);

    view.RunVcsSequenceStepForTesting(ned::ui::BufferView::VcsSequenceStep::Continue);
    REQUIRE(PumpUntil(fixture.loop, [&statusMessage] { return !statusMessage.empty(); }));
    CHECK(statusMessage == "Still conflicted: file.txt");

    // Resolved on the host, it is staged and the merge concluded.
    std::ofstream(fixture.file) << "line one BOTH\nline two\nline three\n";
    ned::editor::vcs::SetSequenceAutoStage(true);
    statusMessage.clear();
    view.RunVcsSequenceStepForTesting(ned::ui::BufferView::VcsSequenceStep::Continue);
    // --no-optional-locks: a plain status takes the index lock the stage needs.
    const auto clean     = [&] { return Run({"git", "--no-optional-locks", "-C", fixture.repo.string(), "status", "--porcelain"}).empty(); };
    const bool concluded = PumpUntil(fixture.loop, clean);
    ned::editor::vcs::SetSequenceAutoStage(false);
    CAPTURE(statusMessage);
    CHECK(concluded);
    CHECK_THAT(Run({"git", "-C", fixture.repo.string(), "log", "-1", "--pretty=%s"}), ContainsSubstring("Merge branch 'other'"));
}

TEST_CASE("Opening a host's conflicted file lands on its first hunk", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture fixture;
    MergeConflict(fixture.repo);
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);

    ned::text::BufferList bufferList;
    ned::text::Buffer&    remoteBuffer = bufferList.AddRemotePlaceholder(ned::text::RemoteFile{.destination = "host", .path = fixture.file});
    remoteBuffer.FinishLoad(ned::text::Rope(Run({"cat", fixture.file.string()})));
    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::editor::CommandRegistry registry;
    ned::editor::Keymap          keymap;
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();
    std::string                  statusMessage;
    ned::ui::ActiveBuffer        activeBuffer{remoteBuffer};
    ned::ui::BufferView          view(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    view.SetVcsRunner(&runner);
    REQUIRE(remoteBuffer.Point() == 0);

    view.RefreshVcsDiff();
    REQUIRE(PumpUntil(fixture.loop, [&statusMessage] { return statusMessage.starts_with("merge conflict"); }));
    CHECK(remoteBuffer.Text().substr(remoteBuffer.Point()).starts_with("<<<<<<<"));
}

TEST_CASE("The VCS panel marks a host's conflicted file and resolves it in its remote buffer", "[Remote][RemoteVcs][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }
    Fixture fixture;
    MergeConflict(fixture.repo);
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);

    ned::text::BufferList list;
    ned::text::Buffer&    other = list.AddRemotePlaceholder(ned::text::RemoteFile{.destination = "host", .path = fixture.repo / "other.txt"});
    other.FinishLoad(ned::text::Rope("other\n"));
    ned::ui::ActiveBuffer activeBuffer(other);
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    std::string           statusMessage;
    ned::ui::VcsPanel     panel([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; }, list, statusMessage, theme);
    panel.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 19});
    panel.SetVcsRunner(&runner);
    std::optional<ned::text::RemoteFile> opened;
    panel.SetOnOpenRemoteFile([&opened](const ned::text::RemoteFile& file) { opened = file; });

    ned::ui::Screen screen(40, 20);
    const auto      shows = [&](const std::string& text) {
        panel.Paint(ned::ui::Canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 19}));
        for (int row = 0; row < 20; ++row) {
            std::string line;
            for (int col = 0; col < 40; ++col) {
                line += screen.PixelAt(col, row).character;
            }
            if (line.contains(text)) {
                return true;
            }
        }
        return false;
    };
    REQUIRE(PumpUntil(fixture.loop, [&] { return shows("file.txt ⚠"); }));

    // Not open yet: asked for, and resolved once it has loaded.
    panel.ResolveAllConflicts(fixture.file, ned::editor::ConflictResolution::TakeOurs);
    CHECK(opened == ned::text::RemoteFile{.destination = "host", .path = fixture.file});
    CHECK(statusMessage == "opening \"file.txt\" -- resolve it once loaded");

    ned::text::Buffer& conflicted = list.AddRemotePlaceholder(*opened);
    conflicted.FinishLoad(ned::text::Rope(Run({"cat", fixture.file.string()})));
    panel.ResolveAllConflicts(fixture.file, ned::editor::ConflictResolution::TakeOurs);
    CHECK(&activeBuffer.Get() == &conflicted);
    CHECK(conflicted.Text() == "line one MAIN\nline two\nline three\n");
}

TEST_CASE("The Remote panel colours a root's rows by its repository's status on the host", "[Remote][RemoteVcs][RemotePanel][HostAgent]") {
    if (!GitAvailable()) {
        SKIP("git is not installed");
    }
    Fixture fixture;
    std::ofstream(fixture.repo / "new.txt") << "untracked\n";
    vcs::Runner runner(fixture.loop);
    runner.SetHosts(&fixture.hosts);
    const ned::ui::Theme theme = ned::ui::DarkTheme();
    ned::ui::RemotePanel panel(theme, fixture.hosts);
    panel.SetVcsRunner(&runner);
    panel.Tree().SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 9});
    panel.Tree().SetDrawBorder(false);
    panel.OpenRoot("host:" + fixture.repo.string());

    // The foreground a row's label is painted in, once that row shows.
    const auto labelColour = [&](const std::string& name) -> std::optional<ned::ui::Color> {
        ned::ui::Screen screen{60, 10};
        panel.Tree().Paint(ned::ui::Canvas(screen, panel.Tree().Box_()));
        for (int y = 0; y < 10; ++y) {
            std::string row;
            for (int x = 0; x < 60; ++x) {
                row += screen.PixelAt(x, y).character;
            }
            if (row.find(name) != std::string::npos) {
                for (int x = 0; x < 59; ++x) {
                    if (screen.PixelAt(x, y).character == std::string(1, name.front()) && screen.PixelAt(x + 1, y).character == std::string(1, name[1])) {
                        return screen.PixelAt(x, y).foreground_color;
                    }
                }
            }
        }
        return std::nullopt;
    };
    REQUIRE(PumpUntil(fixture.loop, [&] { return labelColour("file.txt") == theme.vcsModifiedForeground; }));
    CHECK(labelColour("new.txt") == theme.vcsUntrackedForeground);
    CHECK(labelColour("src") != theme.vcsModifiedForeground);
}
