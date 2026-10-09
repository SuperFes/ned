//
// remote-connect, remote-disconnect and remote-project-search, typed into a
// BufferView the way a user runs them, against ned_fake_ssh and the real
// agent.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "Editor/BufferSave.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/FormatOnSave.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/PostQueue.h"
#include "Editor/Project/Registry.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/RemoteBuffer.h"
#include "FakeRemoteHost.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/LeftDock.h"
#include "UI/ListPopup.h"
#include "UI/RemotePanel.h"
#include "UI/Theme.h"

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using ned::tests::PumpUntil;
using ned::ui::BufferView;
namespace test = ned::ui::test;

namespace {

struct Fixture {
    ned::tests::FakeRemoteHost    fake;
    ned::tests::ScopedEnvOverride home{"HOME", (fake.dir / "home").c_str()};
    ned::editor::PostQueue        queue;
    ned::editor::remote::Hosts    hosts{queue, [this](const std::string& destination) { return fake.Options(destination); },
                                        ned::tests::FakeResolver(fake.dir)};

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
    CHECK(fixture.statusMessage.ends_with(" s"));

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

TEST_CASE("remote-project-search finds what a remote buffer holds unsaved", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path tree = fixture.fake.dir / "tree";
    std::filesystem::create_directories(tree);
    std::ofstream(tree / "a.txt") << "hay\n";

    fixture.view.VisitRemoteFile({.destination = "host", .path = tree / "a.txt"});
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.activeBuffer.Get().Remote().has_value() && !fixture.activeBuffer.Get().IsLoading(); }));
    fixture.activeBuffer.Get().SetPoint(0);
    fixture.activeBuffer.Get().InsertAtPoint("needle unsaved\n");

    fixture.Command("remote-project-search");
    fixture.Enter("host:" + tree.string() + "/");
    fixture.Enter("needle");
    REQUIRE(fixture.StatusStartsWith("1 match for \"needle\""));
    CHECK_THAT(fixture.activeBuffer.Get().Text(), ContainsSubstring("host:" + (tree / "a.txt").string() + ":1: needle unsaved\n"));
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

TEST_CASE("The host prompts show where ssh_config takes a host, and offer projects on hosts first", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path home = fixture.fake.dir / "home";
    std::filesystem::create_directories(home / ".ssh");
    std::ofstream(home / ".ssh" / "config") << "Host zqalias\n  HostName zq.example.com\n  Port 2200\n  User me\n";
    std::ofstream(home / ".ssh" / "known_hosts") << "[zq.example.com]:2200 ssh-ed25519 AAAA\n[zqother.example]:2201 ssh-ed25519 AAAA\n";
    const ned::tests::ScopedEnvOverride homeOverride("HOME", home.c_str());
    const ned::tests::ScopedEnvOverride state("XDG_STATE_HOME", (fixture.fake.dir / "state").c_str());
    ned::editor::ResetProjectRegistryForTesting();
    struct RegistryGuard {
        ~RegistryGuard() {
            ned::editor::ResetProjectRegistryForTesting();
        }
    } registryGuard;
    ned::editor::RegisterProject("zqproj", "zqalias:/srv/zq");
    ned::ui::RemotePanel panel(fixture.theme, fixture.hosts);
    fixture.view.SetRemotePanel(&panel);

    std::optional<ned::ui::ListPopupModel> popup;
    fixture.view.SetOnCandidatesChanged([&](std::optional<ned::ui::ListPopupModel> model) { popup = std::move(model); });

    fixture.Command("remote-open-root");
    REQUIRE(popup.has_value());
    REQUIRE(popup->rows.size() >= 3);
    CHECK(popup->rows[0].main == "zqalias:/srv/zq");
    CHECK(popup->rows[0].right == "zqproj");
    CHECK(popup->rows[1].main == "zqalias");
    CHECK(popup->rows[1].right == "me@zq.example.com:2200");
    // known_hosts' own name for zqalias isn't offered again.
    CHECK(popup->rows[2].main == "ssh://zqother.example:2201");
    CHECK(popup->rows[2].right.empty());
    fixture.Type("zqpr");
    REQUIRE(popup->rows.size() == 1);
    CHECK(popup->rows[0].right == "zqproj");
    fixture.view.OnEvent(test::Ctrl('g'));

    // Connecting offers a project's host.
    fixture.Command("remote-connect");
    CHECK(popup->rows.at(0).main == "zqalias");
    CHECK(popup->rows.at(0).right == "zqproj");
}

TEST_CASE("open-project takes a host's directory, which opens in the Remote panel", "[BufferView][Remote][HostAgent]") {
    Fixture                             fixture;
    const ned::tests::ScopedEnvOverride state("XDG_STATE_HOME", (fixture.fake.dir / "state").c_str());
    ned::editor::ResetProjectRegistryForTesting();
    struct RegistryGuard {
        ~RegistryGuard() {
            ned::editor::ResetProjectRegistryForTesting();
        }
    } registryGuard;
    ned::ui::RemotePanel        panel(fixture.theme, fixture.hosts);
    const std::filesystem::path dir = fixture.fake.dir / "proj";
    std::filesystem::create_directories(dir);
    fixture.view.SetRemotePanel(&panel);
    const ned::text::RemoteFile root{.destination = "host", .path = dir};

    fixture.Command("open-project");
    fixture.Enter(root.ToString() + "/");
    CHECK(fixture.statusMessage == "Project name: proj");
    fixture.view.OnEvent(test::Return());
    CHECK(fixture.statusMessage == "Opened " + root.ToString() + " in the Remote panel.");
    CHECK(panel.Roots() == std::vector{root});
    const std::vector<ned::editor::ProjectRegistryEntry> projects = ned::editor::ListProjects();
    REQUIRE(projects.size() == 1);
    CHECK(projects[0].name == "proj");
    CHECK(projects[0].root == root.ToString());

    // Registered: switch-project opens it again.
    fixture.Command("switch-project");
    fixture.view.OnEvent(test::Return());
    CHECK(fixture.statusMessage == "Opened " + root.ToString() + " in the Remote panel.");
    CHECK(panel.Roots() == std::vector{root});
}

TEST_CASE("find-file completes a host's path from its listing, connecting first", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path dir = fixture.fake.dir / "proj";
    std::filesystem::create_directories(dir / "sub");
    for (const char* name : {"alpha.txt", "alps.md", "beta.txt"}) {
        std::ofstream(dir / name) << "x\n";
    }
    const std::string root = "host:" + dir.string() + "/";

    std::optional<ned::ui::ListPopupModel> popup;
    fixture.view.SetOnCandidatesChanged([&](std::optional<ned::ui::ListPopupModel> model) { popup = std::move(model); });
    const auto rowsAre = [&](std::vector<std::string> expected) {
        return PumpUntil(fixture.queue, [&] {
            if (!popup || popup->rows.size() != expected.size()) {
                return false;
            }
            return std::ranges::equal(popup->rows, expected, {}, &ned::ui::ListPopupRow::main);
        });
    };

    fixture.Command("find-file");
    fixture.Type(root + "al");
    REQUIRE(rowsAre({"alpha.txt", "alps.md"}));
    fixture.view.OnEvent(test::Tab());
    CHECK(fixture.statusMessage == "Find file: " + root + "alpha.txt");

    // The same directory answers from its listing; a directory gets a slash.
    fixture.view.OnEvent(test::Backspace());
    for (int i = 0; i < 8; ++i) {
        fixture.view.OnEvent(test::Backspace());
    }
    CHECK(fixture.statusMessage == "Find file: " + root);
    REQUIRE(rowsAre({"alpha.txt", "alps.md", "beta.txt", "sub/"}));
    fixture.view.OnEvent(test::Ctrl('g'));

    // A search root is a directory, so only directories are offered.
    fixture.Command("remote-project-search");
    fixture.Type(root);
    REQUIRE(rowsAre({"sub/"}));
}

TEST_CASE("remote-open-root adds a host's directory to the Remote panel, completing directories", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    ned::ui::RemotePanel        panel(fixture.theme, fixture.hosts);
    const std::filesystem::path dir = fixture.fake.dir / "proj";
    std::filesystem::create_directories(dir / "src");
    std::ofstream(dir / "readme.txt") << "x\n";
    fixture.view.SetRemotePanel(&panel);

    std::optional<ned::ui::ListPopupModel> popup;
    fixture.view.SetOnCandidatesChanged([&](std::optional<ned::ui::ListPopupModel> model) { popup = std::move(model); });
    fixture.Command("remote-open-root");
    CHECK_THAT(fixture.statusMessage, StartsWith("Open remote root (host:/path, or a host for its home): "));
    fixture.Type("host:" + dir.string() + "/");
    REQUIRE(PumpUntil(fixture.queue, [&] { return popup && popup->rows.size() == 1 && popup->rows[0].main == "src/"; }));
    fixture.view.OnEvent(test::Tab());
    fixture.view.OnEvent(test::Return());
    CHECK(panel.Roots() == std::vector<ned::text::RemoteFile>{{.destination = "host", .path = dir / "src"}});

    // Without a panel there's nothing to add to.
    fixture.view.SetRemotePanel(nullptr);
    fixture.Command("remote-open-root");
    CHECK(fixture.statusMessage == "No Remote panel.");
}

TEST_CASE("C-c R switches the dock to the Remote panel, then collapses it on a second press", "[BufferView][Remote]") {
    Fixture              fixture;
    ned::ui::RemotePanel panel(fixture.theme, fixture.hosts);
    ned::ui::TreeView    files(fixture.theme);
    ned::ui::LeftDock    dock(fixture.theme);
    const std::size_t    filesId  = dock.AddPanel(U'F', "Files", files);
    const std::size_t    remoteId = dock.AddPanel(U'R', "Remote", panel.Tree());
    fixture.view.SetRemotePanel(&panel);
    fixture.view.SetLeftDock(&dock);
    REQUIRE(dock.ActivePanel() == filesId);

    fixture.view.OnEvent(test::Ctrl('c'));
    fixture.view.OnEvent(test::Character("R"));
    REQUIRE_FALSE(dock.Collapsed());
    REQUIRE(dock.ActivePanel() == remoteId);

    fixture.view.OnEvent(test::Ctrl('c'));
    fixture.view.OnEvent(test::Character("R"));
    REQUIRE(dock.Collapsed());
}

TEST_CASE("The host prompts add names the resolver knows under the search domains", "[BufferView][Remote]") {
    Fixture fixture;
    std::ofstream(fixture.fake.dir / "resolv.conf") << "search example.test other.test\n";

    std::optional<ned::ui::ListPopupModel> popup;
    fixture.view.SetOnCandidatesChanged([&](std::optional<ned::ui::ListPopupModel> model) { popup = std::move(model); });
    const auto rows = [&] {
        std::vector<std::string> labels;
        for (const ned::ui::ListPopupRow& row : popup ? popup->rows : std::vector<ned::ui::ListPopupRow>{}) {
            labels.push_back(row.main);
        }
        return labels;
    };

    // After whatever the system's own ssh files offer.
    const auto endsWith = [&](const std::vector<std::string>& tail) {
        const std::vector<std::string> shown = rows();
        return shown.size() >= tail.size() && std::equal(tail.begin(), tail.end(), shown.end() - static_cast<std::ptrdiff_t>(tail.size()));
    };

    fixture.Command("remote-connect");
    fixture.Type("good");
    REQUIRE(PumpUntil(fixture.queue, [&] { return endsWith({"good.example.test", "good.other.test"}); }));

    // Only the latest text's answer counts.
    fixture.Type("x");
    REQUIRE_FALSE(PumpUntil(fixture.queue, [&] { return std::ranges::contains(rows(), "goodx.example.test"); }, std::chrono::milliseconds(300)));
    REQUIRE_FALSE(std::ranges::contains(rows(), "good.example.test"));
    fixture.view.OnEvent(test::Ctrl('g'));

    fixture.Command("remote-project-search");
    fixture.Type("good.example");
    REQUIRE(PumpUntil(fixture.queue, [&] { return endsWith({"good.example.example.test:/", "good.example.other.test:/"}); }));
    // Up from the top wraps to the last row.
    fixture.view.OnEvent(test::ArrowUp());
    fixture.view.OnEvent(test::ArrowUp());
    fixture.view.OnEvent(test::Tab());
    REQUIRE(fixture.statusMessage == "Remote search in (host:/path): good.example.example.test:/");
    fixture.view.OnEvent(test::Ctrl('g'));
}

TEST_CASE("find-file opens a host:/path through the host, once, and a missing one as a new remote file", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path file = fixture.fake.dir / "code.py";
    std::ofstream(file, std::ios::binary) << "x = 1\r\ny = 2\r\n";
    const std::string where = "host:" + file.string();

    fixture.Command("find-file");
    fixture.Enter(where);
    REQUIRE(fixture.statusMessage == "Reading " + where + "...");
    REQUIRE(fixture.activeBuffer.Get().IsLoading());
    REQUIRE(fixture.StatusStartsWith("Opened " + where));

    const ned::text::Buffer& opened = fixture.activeBuffer.Get();
    CHECK(opened.Text() == "x = 1\ny = 2\n");
    CHECK(opened.Name() == "host:code.py");
    CHECK_FALSE(opened.Path().has_value());
    REQUIRE(opened.Remote().has_value());
    CHECK(opened.Remote()->ToString() == where);
    CHECK(opened.LineEndingKind() == ned::text::LineEnding::CRLF);
    CHECK(opened.RemoteTimestamp() == std::filesystem::last_write_time(file));
    CHECK_FALSE(opened.IsLoading());
    CHECK_FALSE(opened.ReadOnly());
    CHECK_FALSE(opened.Modified());
    // Its mode comes from the host path and its content, as a local file's would.
    CHECK(ned::editor::CachedModeForBuffer(opened).name == ned::editor::ModeForPath(file).name);
    CHECK(ned::editor::CachedModeForBuffer(opened).name != ned::editor::FundamentalMode().name);

    // Already open: the same buffer, no second read.
    const std::size_t buffers = fixture.bufferList.Buffers().size();
    fixture.activeBuffer.Set(fixture.buffer);
    fixture.Command("find-file");
    fixture.Enter(where);
    CHECK(&fixture.activeBuffer.Get() == &opened);
    CHECK(fixture.bufferList.Buffers().size() == buffers);

    const std::string fresh = "host:" + (fixture.fake.dir / "new.txt").string();
    fixture.Command("find-file");
    fixture.Enter(fresh);
    REQUIRE(fixture.StatusStartsWith("(New file) " + fresh));
    CHECK(fixture.activeBuffer.Get().Text().empty());
    CHECK_FALSE(fixture.activeBuffer.Get().RemoteTimestamp().has_value());
    CHECK_FALSE(std::filesystem::exists(fixture.fake.dir / "new.txt"));
}

TEST_CASE("find-file opens a file on another port through an ssh URI", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path file = fixture.fake.dir / "code.py";
    std::ofstream(file) << "x = 1\n";
    const std::string where = "ssh://me@host:2222" + file.string();

    fixture.Command("find-file");
    fixture.Enter(where);
    REQUIRE(fixture.StatusStartsWith("Opened " + where));
    const ned::text::Buffer& opened = fixture.activeBuffer.Get();
    CHECK(opened.Text() == "x = 1\n");
    CHECK(opened.Name() == "host:2222:code.py");
    CHECK(opened.Remote() == ned::text::RemoteFile{.destination = "ssh://me@host:2222", .path = file});
    CHECK(fixture.hosts.Destinations() == std::vector<std::string>{"ssh://me@host:2222"});
}

TEST_CASE("project-find-references with no server searches a remote buffer's project on its host", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path project = fixture.fake.dir / "proj";
    std::filesystem::create_directories(project / ".git");
    std::filesystem::create_directories(project / "src");
    std::ofstream(project / "src" / "a.c") << "int needle;\n";
    std::ofstream(project / "src" / "b.c") << "// b\nuse(needle);\nneedles();\n";
    std::ofstream(fixture.fake.dir / "outside.c") << "needle\n";

    fixture.view.VisitRemoteFile({.destination = "host", .path = project / "src" / "a.c"});
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.activeBuffer.Get().Remote().has_value() && !fixture.activeBuffer.Get().IsLoading(); }));
    fixture.activeBuffer.Get().SetPoint(5);

    fixture.Command("project-find-references");
    REQUIRE(fixture.StatusStartsWith("2 references to \"needle\""));
    const std::string results = fixture.activeBuffer.Get().Text();
    CHECK_THAT(results, ContainsSubstring("host:" + (project / "src" / "a.c").string() + ":1: int needle;\n"));
    CHECK_THAT(results, ContainsSubstring("host:" + (project / "src" / "b.c").string() + ":2: use(needle);\n"));
    CHECK(results.find("outside.c") == std::string::npos);
}

TEST_CASE("project-replace on a remote buffer reviews its host project's matches in remote buffers", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path project = fixture.fake.dir / "proj";
    std::filesystem::create_directories(project / ".git");
    std::filesystem::create_directories(project / "src");
    std::ofstream(project / "src" / "a.c") << "foo one\n";
    std::ofstream(project / "src" / "b.c") << "two foo\n";
    std::ofstream(project / "src" / "c.c") << "nothing\n";

    fixture.view.VisitRemoteFile({.destination = "host", .path = project / "src" / "a.c"});
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.activeBuffer.Get().Remote().has_value() && !fixture.activeBuffer.Get().IsLoading(); }));
    ned::text::Buffer& a = fixture.activeBuffer.Get();
    // Unsaved, and searched as it is.
    a.SetPoint(a.Size());
    a.InsertAtPoint("foo three\n");

    fixture.Command("project-replace");
    fixture.Enter("foo");
    fixture.Enter("bar");
    REQUIRE(fixture.StatusStartsWith("3 replacements in 3 lines across 2 files"));
    ned::text::Buffer& review = fixture.activeBuffer.Get();
    CHECK(review.Name() == "*project replace*");
    CHECK_THAT(review.Text(), ContainsSubstring("host:" + (project / "src" / "b.c").string() + ":1"));

    const ned::editor::multibuffer::CommitResult committed =
        ned::editor::multibuffer::CommitExcerptChanges(fixture.bufferList, review, nullptr, nullptr, ned::editor::multibuffer::CommitTarget::Disk);
    CHECK(committed.skipped.empty());
    CHECK(a.Text() == "bar one\nbar three\n");
    ned::text::Buffer* b = fixture.bufferList.FindByRemote({.destination = "host", .path = project / "src" / "b.c"});
    REQUIRE(b != nullptr);
    CHECK(b->Text() == "two bar\n");
    CHECK(b->Modified());
    // Nothing on the host changes until they're saved.
    std::ifstream in(project / "src" / "b.c");
    CHECK(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) == "two foo\n");
}

TEST_CASE("find-file on a host:/path that can't be read closes its buffer and says why", "[BufferView][Remote][HostAgent]") {
    Fixture fixture;
    const auto remoteBuffers = [&fixture] {
        return std::ranges::count_if(fixture.bufferList.Buffers(), [](const auto& buffer) { return buffer->Remote().has_value(); });
    };

    fixture.Command("find-file");
    fixture.Enter("host:" + fixture.fake.dir.string());
    REQUIRE(fixture.StatusStartsWith("Couldn't open host:" + fixture.fake.dir.string() + ": " + fixture.fake.dir.string() + " is a directory"));
    CHECK(remoteBuffers() == 0);

    fixture.Command("find-file");
    fixture.Enter("denied:/etc/hosts");
    REQUIRE(fixture.StatusStartsWith("Couldn't open denied:/etc/hosts: "));
    CHECK_THAT(fixture.statusMessage, ContainsSubstring("Permission denied"));
    CHECK(remoteBuffers() == 0);

    std::ofstream(fixture.fake.dir / "blob", std::ios::binary) << std::string("\x7f" "ELF\0\0", 6);
    fixture.Command("find-file");
    fixture.Enter("host:" + (fixture.fake.dir / "blob").string());
    REQUIRE(fixture.StatusStartsWith("Couldn't open host:"));
    CHECK_THAT(fixture.statusMessage, ContainsSubstring("refusing to open binary file as text"));
    CHECK(remoteBuffers() == 0);
}

TEST_CASE("Closing a remote buffer while it loads drops the host's answer", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path file = fixture.fake.dir / "a.txt";
    std::ofstream(file) << "text\n";

    fixture.Command("find-file");
    fixture.Enter("host:" + file.string());
    REQUIRE(fixture.activeBuffer.Get().IsLoading());
    fixture.Command("kill-buffer");
    // The kill-buffer prompt names the active buffer by default.
    fixture.view.OnEvent(test::Return());
    REQUIRE(fixture.bufferList.FindByRemote({.destination = "host", .path = file}) == nullptr);
    // The connection and read still finish; nothing reappears.
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.hosts.Find("host") != nullptr; }));
    REQUIRE_FALSE(PumpUntil(fixture.queue, [&] { return fixture.bufferList.FindByRemote({.destination = "host", .path = file}) != nullptr; },
                            std::chrono::milliseconds(500)));
}

TEST_CASE("save-buffer writes a remote buffer to its host, and asks before overwriting a change there", "[BufferView][Remote][HostAgent]") {
    Fixture                          fixture;
    ned::editor::remote::RemoteSaver saver(fixture.hosts, fixture.bufferList, fixture.queue, [&](std::string message) { fixture.statusMessage = std::move(message); });
    ned::editor::SetRemoteSaveDispatcher([&saver](ned::editor::RemoteSaveRequest request) { return saver.Dispatch(std::move(request)); });
    struct DispatcherGuard {
        ~DispatcherGuard() {
            ned::editor::SetRemoteSaveDispatcher(nullptr);
        }
    } guard;

    const std::filesystem::path file = fixture.fake.dir / "notes.txt";
    std::ofstream(file, std::ios::binary) << "one\r\n";
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::group_read);
    const std::string where = "host:" + file.string();
    const auto        read  = [&file] {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };

    fixture.Command("find-file");
    fixture.Enter(where);
    REQUIRE(fixture.StatusStartsWith("Opened " + where));
    ned::text::Buffer& buffer = fixture.activeBuffer.Get();
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("two\n");

    fixture.Command("save-buffer");
    CHECK(fixture.statusMessage == "Saving host:notes.txt...");
    REQUIRE(fixture.StatusStartsWith("Wrote " + where));
    CHECK(read() == "one\r\ntwo\r\n");
    CHECK_FALSE(buffer.Modified());
    CHECK(buffer.RemoteTimestamp() == std::filesystem::last_write_time(file));
    CHECK((std::filesystem::status(file).permissions() & std::filesystem::perms::group_read) != std::filesystem::perms::none);

    // Someone else writes it on the host.
    std::ofstream(file, std::ios::binary) << "theirs\n";
    std::filesystem::last_write_time(file, *buffer.RemoteTimestamp() + std::chrono::seconds(5));
    buffer.InsertAtPoint("three\n");
    fixture.Command("save-buffer");
    REQUIRE(fixture.StatusStartsWith("Couldn't save " + where + ": "));
    CHECK_THAT(fixture.statusMessage, ContainsSubstring("saving again asks to overwrite it"));
    CHECK(read() == "theirs\n");
    CHECK(buffer.Modified());

    fixture.Command("save-buffer");
    CHECK(fixture.statusMessage == "host:notes.txt changed on its host since it was read; save anyway? (y/n)");
    fixture.view.OnEvent(test::Character('y'));
    REQUIRE(fixture.StatusStartsWith("Wrote " + where));
    CHECK(read() == "one\r\ntwo\r\nthree\r\n");
    CHECK_FALSE(buffer.ExternallyModified());
    CHECK_FALSE(buffer.Modified());
}

TEST_CASE("save-buffer encodes a large remote buffer off the main thread, then writes it", "[BufferView][Remote][HostAgent]") {
    Fixture                          fixture;
    ned::editor::remote::RemoteSaver saver(fixture.hosts, fixture.bufferList, fixture.queue, [&](std::string message) { fixture.statusMessage = std::move(message); });
    ned::editor::SetRemoteSaveDispatcher([&saver](ned::editor::RemoteSaveRequest request) { return saver.Dispatch(std::move(request)); });
    const std::uintmax_t threshold = ned::text::AsyncLoadThreshold();
    struct Restore {
        std::uintmax_t threshold;
        ~Restore() {
            ned::editor::SetRemoteSaveDispatcher(nullptr);
            ned::text::SetAsyncLoadThreshold(threshold);
        }
    } restore{threshold};
    ned::text::SetAsyncLoadThreshold(1024);

    const std::filesystem::path file = fixture.fake.dir / "big.txt";
    std::ofstream(file, std::ios::binary) << "start\r\n";
    fixture.Command("find-file");
    fixture.Enter("host:" + file.string());
    REQUIRE(fixture.StatusStartsWith("Opened host:" + file.string()));
    ned::text::Buffer& buffer = fixture.activeBuffer.Get();
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint(std::string(4096, 'z') + "\n");

    fixture.Command("save-buffer");
    REQUIRE(fixture.StatusStartsWith("Wrote host:" + file.string()));
    std::ifstream in(file, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) == "start\r\n" + std::string(4096, 'z') + "\r\n");
    CHECK_FALSE(buffer.Modified());
}

TEST_CASE("save-buffer runs a remote buffer's format command on its host, then saves", "[BufferView][Remote][HostAgent]") {
    Fixture                          fixture;
    ned::editor::remote::RemoteSaver saver(fixture.hosts, fixture.bufferList, fixture.queue, [&](std::string message) { fixture.statusMessage = std::move(message); });
    ned::editor::SetRemoteSaveDispatcher([&saver](ned::editor::RemoteSaveRequest request) { return saver.Dispatch(std::move(request)); });
    struct Guard {
        ~Guard() {
            ned::editor::SetRemoteSaveDispatcher(nullptr);
            ned::editor::SetFormatCommand(std::nullopt);
        }
    } guard;

    const std::filesystem::path project = fixture.fake.dir / "project";
    std::filesystem::create_directories(project / ".git");
    const std::filesystem::path file = project / "notes.txt";
    std::ofstream(file, std::ios::binary) << "one\n";
    const std::string where = "host:" + file.string();
    const auto        read  = [&file] {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };

    fixture.Command("find-file");
    fixture.Enter(where);
    REQUIRE(fixture.StatusStartsWith("Opened " + where));
    ned::text::Buffer& buffer = fixture.activeBuffer.Get();
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("two\n");

    // Only on the host is there a .git beside it to find.
    ned::editor::SetFormatCommand(std::string("test -d .git && tr a-z A-Z"));
    fixture.Command("save-buffer");
    CHECK(fixture.statusMessage == "Formatting host:notes.txt...");
    REQUIRE(fixture.StatusStartsWith("Wrote " + where));
    CHECK(read() == "ONE\nTWO\n");
    CHECK(buffer.Text() == "ONE\nTWO\n");
    CHECK_FALSE(buffer.Modified());

    SECTION("one undo takes the format back") {
        fixture.view.OnEvent(test::Ctrl('/'));
        CHECK(buffer.Text() == "one\ntwo\n");
    }
    SECTION("an edit while it runs keeps the edit and skips the format") {
        ned::editor::SetFormatCommand(std::string("sleep 0.5; tr A-Z a-z"));
        buffer.InsertAtPoint("three\n");
        fixture.Command("save-buffer");
        buffer.InsertAtPoint("four\n");
        REQUIRE(fixture.StatusStartsWith("Wrote " + where));
        CHECK(read() == "ONE\nTWO\nthree\nfour\n");
        CHECK(buffer.Text() == "ONE\nTWO\nthree\nfour\n");
    }
    SECTION("a failing command still saves") {
        ned::editor::SetFormatCommand(std::string("exit 3"));
        buffer.InsertAtPoint("three\n");
        fixture.Command("save-buffer");
        REQUIRE(fixture.StatusStartsWith("Wrote " + where));
        CHECK(read() == "ONE\nTWO\nthree\n");
    }
    SECTION("the overwrite confirmation's save formats too") {
        std::ofstream(file, std::ios::binary) << "theirs\n";
        std::filesystem::last_write_time(file, *buffer.RemoteTimestamp() + std::chrono::seconds(5));
        buffer.InsertAtPoint("three\n");
        fixture.Command("save-buffer");
        REQUIRE(fixture.StatusStartsWith("Couldn't save " + where + ": "));
        fixture.Command("save-buffer");
        REQUIRE(fixture.statusMessage == "host:notes.txt changed on its host since it was read; save anyway? (y/n)");
        fixture.view.OnEvent(test::Character('y'));
        REQUIRE(fixture.StatusStartsWith("Wrote " + where));
        CHECK(read() == "ONE\nTWO\nTHREE\n");
    }
}

TEST_CASE("format-buffer runs a remote buffer's format command on its host", "[BufferView][Remote][HostAgent]") {
    Fixture fixture;
    struct Guard {
        ~Guard() {
            ned::editor::SetFormatCommand(std::nullopt);
        }
    } guard;
    const std::filesystem::path file = fixture.fake.dir / "notes.txt";
    std::ofstream(file, std::ios::binary) << "one  \n";

    fixture.Command("find-file");
    fixture.Enter("host:" + file.string());
    REQUIRE(fixture.StatusStartsWith("Opened host:"));
    ned::text::Buffer& buffer = fixture.activeBuffer.Get();

    ned::editor::SetFormatCommand(std::string("tr a-z A-Z"));
    fixture.Command("format-buffer");
    CHECK(fixture.statusMessage == "Formatting host:notes.txt...");
    REQUIRE(fixture.StatusStartsWith("Formatted host:notes.txt"));
    CHECK(buffer.Text() == "ONE  \n");
    CHECK(buffer.Modified());

    fixture.Command("format-buffer");
    REQUIRE(fixture.StatusStartsWith("host:notes.txt is already formatted."));

    // As locally, a failing command falls back to ned's own formatting.
    ned::editor::SetFormatCommand(std::string("exit 1"));
    fixture.Command("format-buffer");
    REQUIRE(fixture.StatusStartsWith("The format command failed: it failed or printed nothing -- formatted host:notes.txt natively."));
    CHECK(buffer.Text() == "ONE\n");
}

TEST_CASE("revert-buffer reads a remote buffer from its host again, as one undoable step", "[BufferView][Remote][HostAgent]") {
    Fixture                          fixture;
    ned::editor::remote::RemoteSaver saver(fixture.hosts, fixture.bufferList, fixture.queue, [&](std::string message) { fixture.statusMessage = std::move(message); });
    ned::editor::SetRemoteSaveDispatcher([&saver](ned::editor::RemoteSaveRequest request) { return saver.Dispatch(std::move(request)); });
    struct DispatcherGuard {
        ~DispatcherGuard() {
            ned::editor::SetRemoteSaveDispatcher(nullptr);
        }
    } guard;

    const std::filesystem::path file = fixture.fake.dir / "notes.txt";
    std::ofstream(file, std::ios::binary) << "caf\xC3\xA9\n";
    const std::string where = "host:" + file.string();
    fixture.Command("find-file");
    fixture.Enter(where);
    REQUIRE(fixture.StatusStartsWith("Opened " + where));
    ned::text::Buffer& buffer = fixture.activeBuffer.Get();

    // A save the host refused leaves the buffer marked changed there; reverting settles that.
    std::ofstream(file, std::ios::binary) << "theirs\n";
    std::filesystem::last_write_time(file, *buffer.RemoteTimestamp() + std::chrono::seconds(5));
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("mine\n");
    fixture.Command("save-buffer");
    REQUIRE(fixture.StatusStartsWith("Couldn't save " + where));
    REQUIRE(buffer.ExternallyModified());

    fixture.Command("revert-buffer");
    CHECK(fixture.statusMessage == "Reading " + where + "...");
    REQUIRE(fixture.StatusStartsWith("Reloaded "));
    CHECK(fixture.statusMessage == "Reloaded " + where + ". Undo brings back your edits.");
    CHECK(buffer.Text() == "theirs\n");
    CHECK_FALSE(buffer.Modified());
    CHECK_FALSE(buffer.ExternallyModified());
    CHECK(buffer.RemoteTimestamp() == std::filesystem::last_write_time(file));
    buffer.Undo();
    CHECK(buffer.Text() == "caf\xC3\xA9\nmine\n");

    std::ofstream(file, std::ios::binary) << "caf\xC3\xA9\n";
    fixture.Command("revert-buffer-with-charset");
    fixture.Enter("latin1");
    REQUIRE(fixture.StatusStartsWith("Reloaded "));
    CHECK(fixture.statusMessage == "Reloaded " + where + " as latin1. Undo brings back your edits.");
    CHECK(buffer.Text() == "caf\xC3\x83\xC2\xA9\n");
    CHECK(buffer.FileCharset() == ned::text::Charset::Latin1);

    std::filesystem::remove(file);
    fixture.Command("revert-buffer");
    REQUIRE(fixture.StatusStartsWith("Couldn't reload " + where + ": no such file"));
    CHECK(buffer.Text() == "caf\xC3\x83\xC2\xA9\n");
}

TEST_CASE("revert-buffer tries a remote buffer's failed load again", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path file = fixture.fake.dir / "later.txt";
    std::ofstream(file, std::ios::binary) << "here now\n";
    const ned::text::RemoteFile remote{.destination = "host", .path = file};

    ned::text::Buffer& placeholder = fixture.bufferList.AddRemotePlaceholder(remote);
    placeholder.MarkLoadFailed("Connection refused");
    fixture.activeBuffer.Set(placeholder);

    fixture.Command("revert-buffer-with-charset");
    fixture.Enter("latin1");
    CHECK(fixture.statusMessage == remote.ToString() + " isn't loaded yet; revert-buffer reads it first.");

    fixture.Command("revert-buffer");
    CHECK_FALSE(placeholder.LoadFailure().has_value());
    REQUIRE(fixture.StatusStartsWith("Opened " + remote.ToString()));
    CHECK(placeholder.Text() == "here now\n");
    CHECK_FALSE(placeholder.IsLoading());

    ned::text::Buffer& unreachable = fixture.bufferList.AddRemotePlaceholder({.destination = "denied", .path = "/etc/hosts"});
    unreachable.MarkLoadFailed("Connection refused");
    fixture.activeBuffer.Set(unreachable);
    fixture.Command("revert-buffer");
    REQUIRE(fixture.StatusStartsWith("Couldn't open denied:/etc/hosts: "));
    CHECK(unreachable.IsLoading());
    REQUIRE(unreachable.LoadFailure().has_value());
    CHECK_THAT(*unreachable.LoadFailure(), ContainsSubstring("Permission denied"));
}

TEST_CASE("next-error walks a remote search's results into the host's files", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path tree = fixture.fake.dir / "walk";
    std::filesystem::create_directories(tree / "sub");
    std::ofstream(tree / "a.txt") << "needle one\nhay\nneedle two\n";
    std::ofstream(tree / "sub" / "b.txt") << "a needle\n";

    fixture.Command("remote-project-search");
    fixture.Enter("host:" + tree.string() + "/");
    fixture.Enter("needle");
    REQUIRE(fixture.StatusStartsWith("3 matches for \"needle\""));

    const auto lineOfPoint = [&fixture] {
        const ned::text::Buffer& buffer = fixture.activeBuffer.Get();
        return std::ranges::count(buffer.Text().substr(0, buffer.Point()), '\n') + 1;
    };
    const ned::text::RemoteFile a{.destination = "host", .path = tree / "a.txt"};

    fixture.Command("next-error");
    REQUIRE(fixture.StatusStartsWith("Opened host:" + a.path.string()));
    CHECK(fixture.activeBuffer.Get().Remote() == a);
    CHECK(lineOfPoint() == 1);

    // Already open: straight to the line.
    fixture.Command("next-error");
    CHECK(fixture.activeBuffer.Get().Remote() == a);
    CHECK(lineOfPoint() == 3);

    // Gone from the host since the search: reported, not created.
    std::filesystem::remove(tree / "sub" / "b.txt");
    fixture.Command("next-error");
    REQUIRE(fixture.StatusStartsWith("Couldn't open host:" + (tree / "sub" / "b.txt").string() + ": no such file"));
    CHECK(fixture.bufferList.FindByRemote({.destination = "host", .path = tree / "sub" / "b.txt"}) == nullptr);
}

TEST_CASE("create-directory, rename-file and delete-file act on a host:/path through its host", "[BufferView][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path tree = fixture.fake.dir / "tree";
    std::filesystem::create_directories(tree / "sub");
    std::ofstream(tree / "sub" / "c.txt") << "c\n";
    std::ofstream(tree / "top.txt") << "t\n";
    const auto at = [](const std::filesystem::path& path) { return "host:" + path.string(); };

    fixture.Command("create-directory");
    fixture.Enter(at(tree / "made" / "deeper"));
    REQUIRE(fixture.StatusStartsWith("Created directory " + at(tree / "made" / "deeper")));
    CHECK(std::filesystem::is_directory(tree / "made" / "deeper"));

    // Open buffers follow a rename: the file itself, and one under a
    // renamed directory.
    fixture.Command("find-file");
    fixture.Enter(at(tree / "sub" / "c.txt"));
    REQUIRE(fixture.StatusStartsWith("Opened "));
    ned::text::Buffer& nested = fixture.activeBuffer.Get();
    fixture.Command("find-file");
    fixture.Enter(at(tree / "top.txt"));
    REQUIRE(fixture.StatusStartsWith("Opened "));
    ned::text::Buffer& top = fixture.activeBuffer.Get();

    fixture.Command("rename-file");
    fixture.Enter(at(tree / "sub"));
    fixture.Enter(at(tree / "moved"));
    REQUIRE(fixture.StatusStartsWith("Renamed to " + at(tree / "moved")));
    CHECK(std::filesystem::exists(tree / "moved" / "c.txt"));
    CHECK(nested.Remote()->ToString() == at(tree / "moved" / "c.txt"));
    CHECK(nested.Name() == "host:c.txt");

    // A bare absolute destination stays on the source's host.
    fixture.Command("rename-file");
    fixture.Enter(at(tree / "top.txt"));
    fixture.Enter((tree / "renamed.py").string());
    REQUIRE(fixture.StatusStartsWith("Renamed to " + at(tree / "renamed.py")));
    CHECK(top.Remote()->ToString() == at(tree / "renamed.py"));
    CHECK(top.Name() == "host:renamed.py");
    CHECK(ned::editor::CachedModeForBuffer(top).name == ned::editor::ModeForPath(tree / "renamed.py").name);

    fixture.Command("rename-file");
    fixture.Enter(at(tree / "renamed.py"));
    fixture.Enter("other:" + (tree / "x.py").string());
    CHECK_THAT(fixture.statusMessage, ContainsSubstring("to another host"));
    CHECK(std::filesystem::exists(tree / "renamed.py"));

    fixture.Command("rename-file");
    fixture.Enter(at(tree / "renamed.py"));
    fixture.Enter(at(tree / "moved"));
    REQUIRE(fixture.StatusStartsWith("Couldn't rename " + at(tree / "renamed.py") + ": "));
    CHECK_THAT(fixture.statusMessage, ContainsSubstring("already exists"));

    fixture.Command("delete-file");
    fixture.Enter(at(tree / "moved"));
    REQUIRE(fixture.statusMessage == "Delete \"" + at(tree / "moved") + "\"? (y/n)");
    fixture.Type("y");
    REQUIRE(fixture.StatusStartsWith("Deleted " + at(tree / "moved")));
    CHECK_FALSE(std::filesystem::exists(tree / "moved"));

    fixture.Command("delete-file");
    fixture.Enter(at(tree / "moved"));
    fixture.Type("y");
    REQUIRE(fixture.StatusStartsWith("Couldn't delete " + at(tree / "moved") + ": no such file or directory"));
}
