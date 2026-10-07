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

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/PostQueue.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/BufferSave.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/RemoteBuffer.h"
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
    CHECK(opened.Name() == "code.py");
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
    CHECK(fixture.statusMessage == "Saving notes.txt...");
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
    CHECK(fixture.statusMessage == "notes.txt changed on host since it was read; save anyway? (y/n)");
    fixture.view.OnEvent(test::Character('y'));
    REQUIRE(fixture.StatusStartsWith("Wrote " + where));
    CHECK(read() == "one\r\ntwo\r\nthree\r\n");
    CHECK_FALSE(buffer.ExternallyModified());
    CHECK_FALSE(buffer.Modified());
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
