//
// The tracker actions BufferView starts: a comment's compose buffer, and
// the plain-letter keys of an issue's own buffer. The EventLoop never runs,
// so a post that starts is seen only as its status message and the busy
// issue it leaves behind.
//

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Org.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Tracker/Clocking.h"
#include "Editor/Tracker/CommentBuffer.h"
#include "Editor/Tracker/IssueBuffer.h"
#include "Editor/Tracker/KeyCompletion.h"
#include "Editor/Tracker/Registry.h"
#include "Editor/Tracker/Runner.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/ListPopup.h"
#include "UI/Theme.h"

using ned::ui::BufferView;
namespace tracker = ned::editor::tracker;
namespace test    = ned::ui::test;

namespace {

class CommentingProvider : public tracker::Provider {
  public:
    explicit CommentingProvider(bool comments) : comments_(comments) {
    }
    [[nodiscard]] tracker::CommandSpec ListArgv(const tracker::Connection&, const std::string&) const override {
        return {};
    }
    [[nodiscard]] std::vector<tracker::Issue> ParseList(const std::string&) const override {
        return {};
    }
    [[nodiscard]] std::optional<tracker::CommandSpec> ViewArgv(const tracker::Connection&, const std::string&) const override {
        return std::nullopt;
    }
    [[nodiscard]] tracker::IssueDetail ParseView(const std::string&) const override {
        return {};
    }
    [[nodiscard]] std::vector<tracker::Detected> Detect(const std::vector<std::string>&) const override {
        return {};
    }
    [[nodiscard]] bool Supports(tracker::Capability capability) const override {
        return comments_;
    }
    [[nodiscard]] tracker::CommandSpec TransitionArgv(const tracker::Connection&, const std::string& key, const std::string& id) const override {
        lastAction = "transition " + key + " " + id;
        return tracker::CommandSpec{.argv = {"sleep", "5"}};
    }
    [[nodiscard]] tracker::CommandSpec AssignArgv(const tracker::Connection&, const std::string& key, const std::string& id) const override {
        lastAction = "assign " + key + " " + id;
        return tracker::CommandSpec{.argv = {"sleep", "5"}};
    }
    [[nodiscard]] tracker::CommandSpec TransitionsArgv(const tracker::Connection&, const std::string&) const override {
        return tracker::CommandSpec{.argv = {"sleep", "5"}};
    }

    [[nodiscard]] tracker::CommandSpec WorklogArgv(const tracker::Connection&, const std::string& key, const tracker::Worklog& worklog) const override {
        lastAction = "worklog " + key + " " + std::to_string(worklog.seconds);
        return tracker::CommandSpec{.argv = {"sleep", "5"}};
    }

    mutable std::string                lastAction;
    [[nodiscard]] tracker::CommandSpec CommentArgv(const tracker::Connection&, const std::string&, const std::string& body) const override {
        lastBody = body;
        return tracker::CommandSpec{.argv = {"sleep", "5", "{input-file}"}, .input = body};
    }

    mutable std::string lastBody;

  private:
    bool comments_;
};

struct RegistryGuard {
    RegistryGuard() {
        tracker::ClearRegistry();
    }
    ~RegistryGuard() {
        tracker::ClearRegistry();
    }
};

// DEV-1 fetched by a panel of connection "work", whose provider comments
// or not.
std::shared_ptr<CommentingProvider> RegisterWork(bool comments) {
    auto provider = std::make_shared<CommentingProvider>(comments);
    tracker::RegisterProvider("stub", provider);
    tracker::SetConnection(tracker::Connection{.name = "work", .provider = "stub"});
    tracker::AddPanel(tracker::Panel{.name = "Mine", .connection = "work"});
    tracker::SetPanelIssues("Mine", {tracker::Issue{.key = "DEV-1", .title = "Fix the login"}});
    return provider;
}

struct Fixture {
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

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
    ned::text::Buffer&    original = bufferList.OpenOrCreateFile("/repo/original.txt");
    ned::ui::ActiveBuffer activeBuffer{original};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    }
};

void Type(BufferView& view, std::string_view text) {
    for (const char ch : text) {
        view.OnEvent(test::Character(ch));
    }
}

void InvokeCommand(BufferView& view, std::string_view name) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Alt('x'));
    Type(view, name);
    view.OnEvent(test::Return());
}

} // namespace

TEST_CASE("tracker-comment refuses an issue it can't comment on", "[BufferView][Tracker]") {
    RegistryGuard guard;
    Fixture       fixture;
    BufferView    view = fixture.View();

    view.BeginIssueAction(tracker::IssueAction::Comment, tracker::Issue{.key = "DEV-1"});
    CHECK(fixture.statusMessage == "no tracker panel has fetched DEV-1 -- open it from one first");

    RegisterWork(false);
    view.BeginIssueAction(tracker::IssueAction::Comment, tracker::Issue{.key = "DEV-1"});
    CHECK(fixture.statusMessage == "tracker connection \"work\" can't comment on issues");
    CHECK(&fixture.activeBuffer.Get() == &fixture.original);
}

TEST_CASE("A comment is written in its own buffer and posted from it", "[BufferView][Tracker]") {
    RegistryGuard guard;
    auto          provider = RegisterWork(true);
    Fixture       fixture;
    BufferView    view = fixture.View();

    view.BeginIssueAction(tracker::IssueAction::Comment, tracker::Issue{.key = "DEV-1"});
    ned::text::Buffer& compose = fixture.activeBuffer.Get();
    REQUIRE(compose.Name() == "*comment DEV-1*");
    CHECK(tracker::FindComment(compose) == tracker::CommentTarget{.connection = "work", .key = "DEV-1"});
    CHECK(ned::editor::CachedModeForBuffer(compose).name == tracker::kCommentModeName);

    SECTION("nothing written posts nothing") {
        compose.InsertAtPoint("\n  \n");
        InvokeCommand(view, "tracker-comment-finish");
        CHECK(fixture.statusMessage == "Empty comment -- not posting.");
        CHECK(&fixture.activeBuffer.Get() == &compose);
    }
    SECTION("without a runner the buffer is kept") {
        compose.InsertAtPoint("Done in **abc123**.\n\n");
        InvokeCommand(view, "tracker-comment-finish");
        CHECK(fixture.statusMessage == "no tracker runner configured");
        CHECK(&fixture.activeBuffer.Get() == &compose);
    }
    SECTION("posting keeps the buffer until the tracker answers") {
        ned::ui::EventLoop eventLoop;
        tracker::Runner    runner(eventLoop);
        view.SetTrackerRunner(&runner);
        compose.InsertAtPoint("\nDone in **abc123**.\n\n");
        InvokeCommand(view, "tracker-comment-finish");
        CHECK(fixture.statusMessage == "Posting the comment on DEV-1...");
        CHECK(provider->lastBody == "Done in **abc123**.");
        CHECK(&fixture.activeBuffer.Get() == &compose);

        // A second C-c C-c while the first is in flight is refused.
        InvokeCommand(view, "tracker-comment-finish");
        CHECK(fixture.statusMessage == "tracker issue \"DEV-1\" is busy with a previous request");
    }
    SECTION("discarding closes it") {
        compose.InsertAtPoint("never mind");
        InvokeCommand(view, "tracker-comment-abort");
        CHECK(fixture.statusMessage == "Comment discarded.");
        CHECK(fixture.bufferList.Find("*comment DEV-1*") == nullptr);
    }
    SECTION("commenting again returns to the draft") {
        compose.InsertAtPoint("draft");
        fixture.activeBuffer.Set(fixture.original);
        view.BeginIssueAction(tracker::IssueAction::Comment, tracker::Issue{.key = "DEV-1"});
        CHECK(&fixture.activeBuffer.Get() == &compose);
        CHECK(compose.Text() == "draft");
    }
}

TEST_CASE("Plain letters act on the issue in its own buffer", "[BufferView][Tracker]") {
    RegistryGuard guard;
    RegisterWork(true);
    Fixture    fixture;
    BufferView view = fixture.View();

    ned::text::Buffer& issue =
        tracker::ShowIssue(fixture.bufferList, tracker::IssueDetail{.issue = tracker::Issue{.key = "DEV-1", .title = "Fix the login"}});
    fixture.activeBuffer.Set(issue);

    view.OnEvent(test::Character('c'));
    CHECK(fixture.activeBuffer.Get().Name() == "*comment DEV-1*");

    fixture.activeBuffer.Set(issue);
    view.OnEvent(test::Character('b'));
    // No VCS runner here, which is how far a branch gets.
    CHECK(fixture.statusMessage == "no vcs runner configured");

    // Any other read-only buffer keeps its letters.
    ned::text::Buffer& other = fixture.bufferList.CreateBuffer("*notes*");
    other.SetReadOnly(true);
    fixture.activeBuffer.Set(other);
    fixture.statusMessage.clear();
    view.OnEvent(test::Character('c'));
    CHECK(fixture.activeBuffer.Get().Name() == "*notes*");
}

TEST_CASE("Moving or assigning an issue picks from what the tracker offers", "[BufferView][Tracker]") {
    RegistryGuard guard;
    auto          provider = RegisterWork(true);
    Fixture       fixture;
    BufferView    view = fixture.View();

    SECTION("without a runner nothing is fetched") {
        view.BeginIssueAction(tracker::IssueAction::Transition, tracker::Issue{.key = "DEV-1"});
        CHECK(fixture.statusMessage == "no tracker runner configured");
    }
    SECTION("the transitions are fetched first") {
        ned::ui::EventLoop eventLoop;
        tracker::Runner    runner(eventLoop);
        view.SetTrackerRunner(&runner);
        view.BeginIssueAction(tracker::IssueAction::Transition, tracker::Issue{.key = "DEV-1"});
        CHECK(fixture.statusMessage == "Fetching the statuses DEV-1 can move to...");
    }
    SECTION("the picked transition is the one sent") {
        ned::ui::EventLoop eventLoop;
        tracker::Runner    runner(eventLoop);
        view.SetTrackerRunner(&runner);
        view.BeginIssueChoicePromptForTesting(true, "DEV-1", "work",
                                              {{.id = "11", .name = "To Do"}, {.id = "31", .name = "Done"}, {.id = "32", .name = "Done"}});
        Type(view, "Done [32]");
        view.OnEvent(test::Return());
        CHECK(provider->lastAction == "transition DEV-1 32");
        CHECK(fixture.statusMessage == "Moving DEV-1...");
    }
    SECTION("an assignee likewise") {
        ned::ui::EventLoop eventLoop;
        tracker::Runner    runner(eventLoop);
        view.SetTrackerRunner(&runner);
        // Listed as the tracker gave them, yourself first, not alphabetically.
        view.BeginIssueChoicePromptForTesting(false, "DEV-1", "work",
                                              {{.id = "z1", .name = "Zed (me)"}, {.id = "a1", .name = "Ann"}, {.id = "", .name = "Unassigned"}});
        view.OnEvent(test::Return());
        CHECK(provider->lastAction == "assign DEV-1 z1");
        CHECK(fixture.statusMessage == "Assigning DEV-1...");
    }
}

TEST_CASE("A key being typed completes from the user's own issues", "[BufferView][Tracker]") {
    RegistryGuard guard;
    RegisterWork(true); // DEV is known from the panel's DEV-1
    tracker::SetMineIssues("work", {{.key = "DEV-450", .title = "Login"}, {.key = "OPS-4"}, {.key = "DEV-41", .title = "Logout"}, {.key = "DEV-7"}});
    Fixture    fixture;
    BufferView view = fixture.View();

    std::optional<ned::ui::ListPopupModel> popup;
    view.SetOnCompletionChanged([&popup](std::optional<ned::ui::ListPopupModel> model) { popup = std::move(model); });

    fixture.original.InsertAtPoint("Fixes DEV-4");
    InvokeCommand(view, "lsp-complete");
    REQUIRE(popup);
    std::vector<std::string> rows;
    for (const ned::ui::ListPopupRow& row : popup->rows) {
        rows.push_back(row.main);
    }
    CHECK(rows == std::vector<std::string>{"DEV-450", "DEV-41"});

    view.AcceptActiveCompletionAt(1);
    CHECK(fixture.original.Text() == "Fixes DEV-41");
}

TEST_CASE("open-link-at-point opens the issue a key names", "[BufferView][Tracker]") {
    RegistryGuard guard;
    RegisterWork(true);
    Fixture            fixture;
    BufferView         view = fixture.View();
    ned::ui::EventLoop eventLoop;
    tracker::Runner    runner(eventLoop);
    view.SetTrackerRunner(&runner);

    fixture.original.InsertAtPoint("Blocked on DEV-1 still");
    fixture.original.SetPoint(13);
    InvokeCommand(view, "open-link-at-point");
    // The stub has no detail view, so the issue opens from what the panel listed.
    CHECK(fixture.activeBuffer.Get().Name() == "*issue DEV-1*");
    CHECK(fixture.activeBuffer.Get().Text().starts_with("# DEV-1: Fix the login"));

    // A word that isn't a key still falls through to the ordinary link search.
    fixture.activeBuffer.Set(fixture.original);
    fixture.original.SetPoint(2);
    InvokeCommand(view, "open-link-at-point");
    CHECK(fixture.statusMessage == "No link at point.");
}

namespace {

// Points the issues Org file into a fresh directory.
struct ClockDir {
    std::filesystem::path dir;
    ClockDir() {
        std::string pattern = (std::filesystem::temp_directory_path() / "ned_tracker_clock_XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        dir = pattern;
        tracker::SetClockFile(dir / "issues.org");
    }
    ~ClockDir() {
        tracker::SetClockFile({});
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
    ClockDir(const ClockDir&)            = delete;
    ClockDir& operator=(const ClockDir&) = delete;
};

} // namespace

TEST_CASE("Clocking in on an issue makes its heading and shows the running clock", "[BufferView][Tracker]") {
    RegistryGuard guard;
    ClockDir      clockDir;
    RegisterWork(true);
    Fixture    fixture;
    BufferView view = fixture.View();

    view.BeginIssueAction(tracker::IssueAction::ClockIn, tracker::Issue{.key = "DEV-1", .title = "Fix the login"});
    ned::text::Buffer& issues = fixture.activeBuffer.Get();
    CHECK(issues.Path() == clockDir.dir / "issues.org");
    CHECK(fixture.statusMessage == "Clocked in on DEV-1.");
    CHECK(issues.Text().starts_with("* DEV-1 Fix the login\n:PROPERTIES:\n:ISSUE: DEV-1\n:TRACKER: work\n:END:\n:LOGBOOK:\nCLOCK: ["));
    CHECK(tracker::RunningIssueClock(issues.Text()));

    view.BeginIssueAction(tracker::IssueAction::ClockIn, tracker::Issue{.key = "DEV-1"});
    CHECK(fixture.statusMessage == "Already clocked in on DEV-1.");

    // Stopping straight away has nothing worth logging.
    InvokeCommand(view, "org-clock-out");
    CHECK(fixture.statusMessage == "Clocked out of DEV-1 inside a minute -- nothing to log.");
    CHECK_FALSE(ned::editor::org::CurrentlyRunningClock(issues.Text()));
}

TEST_CASE("Clocking out of an issue offers its time as a worklog", "[BufferView][Tracker]") {
    RegistryGuard guard;
    ClockDir      clockDir;
    auto          provider = RegisterWork(true);
    Fixture       fixture;
    BufferView    view = fixture.View();

    // Clocked in two hours ago, from wherever: tracker-clock-out finds the
    // issues file's clock without it being the active buffer.
    ned::text::Buffer& issues = fixture.bufferList.OpenOrCreateFile(clockDir.dir / "issues.org");
    issues.SetPoint(tracker::EnsureIssueHeading(issues, {.key = "DEV-1"}, "work"));
    REQUIRE(ned::editor::org::ClockInAtPoint(issues, std::chrono::system_clock::now() - std::chrono::minutes(121)).status ==
            ned::editor::org::ClockInStatus::Ok);

    SECTION("without a runner the clock just stops") {
        InvokeCommand(view, "tracker-clock-out");
        CHECK(fixture.statusMessage == "Clocked out of DEV-1 after 2:01.");
    }
    SECTION("logging it posts the interval") {
        ned::ui::EventLoop eventLoop;
        tracker::Runner    runner(eventLoop);
        view.SetTrackerRunner(&runner);
        InvokeCommand(view, "tracker-clock-out");
        CHECK(fixture.statusMessage.starts_with("Clocked out of DEV-1 after 2:01: "));
        view.OnEvent(test::Return());
        CHECK(provider->lastAction == "worklog DEV-1 7260");
        CHECK(fixture.statusMessage == "Logging 2:01 to DEV-1...");
    }
    SECTION("or not") {
        ned::ui::EventLoop eventLoop;
        tracker::Runner    runner(eventLoop);
        view.SetTrackerRunner(&runner);
        InvokeCommand(view, "tracker-clock-out");
        Type(view, "Don't");
        view.OnEvent(test::Return());
        CHECK(fixture.statusMessage == "Not logged.");
        CHECK(provider->lastAction.empty());
    }
    CHECK_FALSE(ned::editor::org::CurrentlyRunningClock(issues.Text()));
}
