#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/Tracker/CommentBuffer.h"
#include "Editor/Tracker/Detect.h"
#include "Editor/Tracker/IssueBuffer.h"
#include "Editor/Tracker/Registry.h"
#include "Editor/Tracker/Runner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

#include <unistd.h>

using Catch::Matchers::ContainsSubstring;
using ned::editor::tracker::Capability;
using ned::editor::tracker::Choice;
using ned::editor::tracker::CommandSpec;
using ned::editor::tracker::Comment;
using ned::editor::tracker::Connection;
using ned::editor::tracker::Detected;
using ned::editor::tracker::FinishList;
using ned::editor::tracker::FinishView;
using ned::editor::tracker::Issue;
using ned::editor::tracker::IssueDetail;
using ned::editor::tracker::Panel;
using ned::editor::tracker::Provider;
using ned::editor::tracker::Runner;

namespace tracker = ned::editor::tracker;

// As in VcsRunnerTest.cpp, the EventLoop never runs here, so a spawned
// command never completes; the completion half is covered through
// FinishList instead.

namespace {

class StubProvider : public Provider {
  public:
    explicit StubProvider(std::vector<std::string> argv = {"sleep", "5"}, bool hasView = true) : argv_(std::move(argv)), hasView_(hasView) {
    }

    [[nodiscard]] CommandSpec ListArgv(const Connection& connection, const std::string& query) const override {
        if (query == "throw") {
            throw std::runtime_error("bad query");
        }
        lastConnection = connection;
        lastQuery      = query;
        return CommandSpec{.argv = argv_, .curlCredentials = curlCredentials};
    }

    [[nodiscard]] std::vector<Issue> ParseList(const std::string& output) const override {
        if (output == "garbage") {
            throw std::runtime_error("unparseable");
        }
        return {Issue{.key = "NED-1", .title = output}};
    }

    [[nodiscard]] std::optional<CommandSpec> ViewArgv(const Connection& connection, const std::string& key) const override {
        if (!hasView_) {
            return std::nullopt;
        }
        if (key == "THROW-1") {
            throw std::runtime_error("bad key");
        }
        lastConnection = connection;
        lastKey        = key;
        return CommandSpec{argv_};
    }

    // The view knows the body and a fresher status; everything else is left
    // for the listed row to fill.
    [[nodiscard]] IssueDetail ParseView(const std::string& output) const override {
        if (output == "garbage") {
            throw std::runtime_error("unparseable");
        }
        return IssueDetail{.issue = Issue{.status = "Done"}, .body = output, .comments = {Comment{.author = "ann", .body = "ok"}}};
    }

    [[nodiscard]] std::vector<Detected> Detect(const std::vector<std::string>& remoteUrls) const override {
        if (throwOnDetect) {
            throw std::runtime_error("detector broke");
        }
        lastRemotes = remoteUrls;
        return detected;
    }

    mutable Connection               lastConnection;
    mutable std::string              lastQuery;
    mutable std::string              lastKey;
    mutable std::vector<std::string> lastRemotes;
    std::vector<Detected>            detected;
    bool                             throwOnDetect   = false;
    bool                             curlCredentials = false;

  private:
    std::vector<std::string> argv_;
    bool                     hasView_ = true;
};

// Supports everything but Assign; each argv echoes what it was given after
// the base argv, so a test can read back what reached the provider.
class ActingProvider : public StubProvider {
  public:
    using StubProvider::StubProvider;

    [[nodiscard]] bool Supports(Capability capability) const override {
        return capability != Capability::Assign;
    }
    [[nodiscard]] CommandSpec TransitionsArgv(const Connection& /*connection*/, const std::string& key) const override {
        return With({"transitions", key});
    }
    [[nodiscard]] std::vector<Choice> ParseTransitions(const std::string& output) const override {
        if (output == "garbage") {
            throw std::runtime_error("unparseable");
        }
        return {Choice{.id = "31", .name = output}};
    }
    [[nodiscard]] CommandSpec TransitionArgv(const Connection& /*connection*/, const std::string& key,
                                             const std::string& transitionId) const override {
        return With({"transition", key, transitionId});
    }
    [[nodiscard]] CommandSpec CommentArgv(const Connection& /*connection*/, const std::string& key, const std::string& body) const override {
        CommandSpec spec = With({"comment", key});
        if (inputPlaceholder) {
            spec.argv.emplace_back("@{input-file}");
        }
        spec.input = body;
        return spec;
    }
    [[nodiscard]] CommandSpec WorklogArgv(const Connection& /*connection*/, const std::string& key, const tracker::Worklog& worklog) const override {
        return With({"worklog", key, std::to_string(worklog.started), std::to_string(worklog.seconds)});
    }
    [[nodiscard]] CommandSpec ProjectKeysArgv(const Connection& /*connection*/) const override {
        return With({"projects"});
    }
    [[nodiscard]] std::vector<std::string> ParseProjectKeys(const std::string& output) const override {
        return {output};
    }
    [[nodiscard]] std::string MineQuery(const Connection& connection) const override {
        return "mine:" + connection.name;
    }

    mutable std::vector<std::string> lastArgs;
    bool                             inputPlaceholder = true;

  private:
    [[nodiscard]] CommandSpec With(std::vector<std::string> args) const {
        lastArgs = args;
        return CommandSpec{.argv = {"sleep", "5"}};
    }
};

// Points $XDG_RUNTIME_DIR, where private files go, at a fresh directory.
class RuntimeDir {
  public:
    RuntimeDir() {
        std::string pattern = (std::filesystem::temp_directory_path() / "ned_tracker_runner_XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        dir_ = pattern;
        if (const char* existing = std::getenv("XDG_RUNTIME_DIR")) {
            previous_ = existing;
        }
        ::setenv("XDG_RUNTIME_DIR", dir_.c_str(), 1);
    }
    ~RuntimeDir() {
        if (previous_) {
            ::setenv("XDG_RUNTIME_DIR", previous_->c_str(), 1);
        }
        else {
            ::unsetenv("XDG_RUNTIME_DIR");
        }
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
    }
    RuntimeDir(const RuntimeDir&)            = delete;
    RuntimeDir& operator=(const RuntimeDir&) = delete;

    [[nodiscard]] std::vector<std::filesystem::path> Files() const {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            files.push_back(entry.path());
        }
        return files;
    }

  private:
    std::filesystem::path      dir_;
    std::optional<std::string> previous_;
};

struct RegistryResetGuard {
    RegistryResetGuard() {
        tracker::ClearRegistry();
    }
    ~RegistryResetGuard() {
        tracker::ClearRegistry();
    }
};

void RegisterStubPanel(const std::shared_ptr<StubProvider>& provider, const std::string& query = "assignee = me") {
    tracker::RegisterProvider("stub", provider);
    tracker::SetConnection(Connection{.name = "work", .provider = "stub", .url = "https://example.atlassian.net", .email = "me@example.com"});
    tracker::AddPanel(Panel{.name = "Mine", .connection = "work", .query = query, .glyph = "J"});
}

struct Outcome {
    std::optional<std::vector<Issue>> issues;
    std::optional<std::string>        error;
};

Outcome Request(Runner& runner, const std::string& panel) {
    Outcome outcome;
    runner.RequestIssues(
        panel, [&outcome](std::vector<Issue> issues) { outcome.issues = std::move(issues); },
        [&outcome](std::string error) { outcome.error = std::move(error); });
    return outcome;
}

} // namespace

TEST_CASE("Tracker registry replaces by name and keeps panel order", "[Tracker]") {
    RegistryResetGuard guard;

    tracker::AddPanel(Panel{.name = "A", .connection = "one"});
    tracker::AddPanel(Panel{.name = "B", .connection = "one"});
    tracker::AddPanel(Panel{.name = "A", .connection = "two", .query = "q"});

    const std::vector<Panel> panels = tracker::Panels();
    REQUIRE(panels.size() == 2);
    CHECK(panels[0] == Panel{.name = "A", .connection = "two", .query = "q"});
    CHECK(panels[1].name == "B");

    CHECK(tracker::RemovePanel("A"));
    CHECK_FALSE(tracker::RemovePanel("A"));
    CHECK_FALSE(tracker::FindPanel("A"));
    CHECK(tracker::FindPanel("B"));

    tracker::SetConnection(Connection{.name = "work", .provider = "jira"});
    tracker::SetConnection(Connection{.name = "work", .provider = "github"});
    REQUIRE(tracker::FindConnection("work"));
    CHECK(tracker::FindConnection("work")->provider == "github");
    CHECK_FALSE(tracker::FindConnection("home"));

    auto first = std::make_shared<StubProvider>();
    tracker::RegisterProvider("stub", first);
    CHECK(tracker::FindProvider("stub") == first);
    auto second = std::make_shared<StubProvider>();
    tracker::RegisterProvider("stub", second);
    CHECK(tracker::FindProvider("stub") == second);
    CHECK(tracker::FindProvider("other") == nullptr);

    tracker::ClearRegistry();
    CHECK(tracker::Panels().empty());
    CHECK_FALSE(tracker::FindConnection("work"));
    CHECK(tracker::FindProvider("stub") == nullptr);
}

TEST_CASE("FinishList parses a clean exit and reports everything else", "[Tracker]") {
    const StubProvider provider;

    SECTION("exit 0 parses the output") {
        const auto result = FinishList(provider, "Mine", "hello", 0);
        REQUIRE(result);
        REQUIRE(result->size() == 1);
        CHECK((*result)[0].title == "hello");
    }
    SECTION("a non-zero exit carries the command's output, trimmed") {
        const auto result = FinishList(provider, "Mine", "gh: not logged in\n", 4);
        REQUIRE_FALSE(result);
        CHECK(result.error() == "tracker panel \"Mine\": fetch failed (exit 4): gh: not logged in");
    }
    SECTION("no exit code and no output means the command never started") {
        const auto result = FinishList(provider, "Mine", "", std::nullopt);
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error(), ContainsSubstring("couldn't start"));
    }
    SECTION("a parse failure is reported, not thrown") {
        const auto result = FinishList(provider, "Mine", "garbage", 0);
        REQUIRE_FALSE(result);
        CHECK(result.error() == "tracker panel \"Mine\": unparseable");
    }
}

TEST_CASE("Runner::RequestIssues resolves panel, connection and provider by name", "[Tracker]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner             runner(eventLoop);

    SECTION("unknown panel") {
        const Outcome outcome = Request(runner, "Nope");
        CHECK_FALSE(outcome.issues);
        CHECK(outcome.error == "no tracker panel named \"Nope\"");
    }
    SECTION("unknown connection") {
        tracker::AddPanel(Panel{.name = "Mine", .connection = "work"});
        const Outcome outcome = Request(runner, "Mine");
        CHECK(outcome.error == "tracker panel \"Mine\": no connection named \"work\"");
    }
    SECTION("unknown provider") {
        tracker::AddPanel(Panel{.name = "Mine", .connection = "work"});
        tracker::SetConnection(Connection{.name = "work", .provider = "jira"});
        const Outcome outcome = Request(runner, "Mine");
        CHECK(outcome.error == "tracker panel \"Mine\": no tracker provider named \"jira\"");
    }
    SECTION("argv is built from the connection and the panel's query") {
        auto provider = std::make_shared<StubProvider>();
        RegisterStubPanel(provider);
        const Outcome outcome = Request(runner, "Mine");
        CHECK_FALSE(outcome.issues);
        CHECK_FALSE(outcome.error);
        CHECK(runner.IsFetching("Mine"));
        CHECK(provider->lastConnection.url == "https://example.atlassian.net");
        CHECK(provider->lastConnection.email == "me@example.com");
        CHECK(provider->lastQuery == "assignee = me");
    }
}

TEST_CASE("Runner::RequestIssues refuses a second fetch of the same panel", "[Tracker]") {
    RegistryResetGuard guard;
    auto               provider = std::make_shared<StubProvider>();
    RegisterStubPanel(provider);
    tracker::AddPanel(Panel{.name = "Other", .connection = "work"});

    ned::ui::EventLoop eventLoop;
    Runner             runner(eventLoop);

    CHECK_FALSE(Request(runner, "Mine").error);
    CHECK(Request(runner, "Mine").error == "tracker panel \"Mine\" is already fetching");
    CHECK_FALSE(Request(runner, "Other").error);
}

TEST_CASE("Runner::RequestIssues reports a throwing argv builder and an unstartable command", "[Tracker]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner             runner(eventLoop);

    SECTION("argv builder throws") {
        RegisterStubPanel(std::make_shared<StubProvider>(), "throw");
        const Outcome outcome = Request(runner, "Mine");
        CHECK(outcome.error == "tracker panel \"Mine\": bad query");
        CHECK_FALSE(runner.IsFetching("Mine"));
    }
    SECTION("executable missing") {
        RegisterStubPanel(std::make_shared<StubProvider>(std::vector<std::string>{"ned-no-such-tracker-cli"}));
        const Outcome outcome = Request(runner, "Mine");
        REQUIRE(outcome.error);
        CHECK_THAT(*outcome.error, ContainsSubstring("couldn't start"));
        CHECK_FALSE(runner.IsFetching("Mine"));
        // A failed start leaves nothing behind to block a retry.
        CHECK(Request(runner, "Mine").error == outcome.error);
    }
}

TEST_CASE("Runner::RequestIssues runs the token command first for a credentialed command", "[Tracker]") {
    RegistryResetGuard guard;
    auto               provider = std::make_shared<StubProvider>(std::vector<std::string>{"curl", "https://example.atlassian.net"});
    provider->curlCredentials   = true;
    RegisterStubPanel(provider);

    ned::ui::EventLoop eventLoop;
    Runner             runner(eventLoop);
    Connection         connection = *tracker::FindConnection("work");

    SECTION("a connection without an email or token command is refused before anything runs") {
        const Outcome outcome = Request(runner, "Mine");
        CHECK(outcome.error == "tracker panel \"Mine\": connection \"work\" needs an :email and a :token-command");
        CHECK_FALSE(runner.IsFetching("Mine"));

        connection.tokenCommand = {"sleep", "5"};
        connection.email.clear();
        tracker::SetConnection(connection);
        CHECK(Request(runner, "Mine").error == "tracker panel \"Mine\": connection \"work\" needs an :email and a :token-command");
    }
    SECTION("the token command holds the panel's fetch") {
        connection.tokenCommand = {"sleep", "5"};
        tracker::SetConnection(connection);
        CHECK_FALSE(Request(runner, "Mine").error);
        CHECK(runner.IsFetching("Mine"));
        CHECK(Request(runner, "Mine").error == "tracker panel \"Mine\" is already fetching");
    }
    SECTION("an unstartable token command is reported") {
        connection.tokenCommand = {"ned-no-such-token-command"};
        tracker::SetConnection(connection);
        const Outcome outcome = Request(runner, "Mine");
        CHECK(outcome.error == "tracker panel \"Mine\": couldn't start the :token-command of connection \"work\"");
        CHECK_FALSE(runner.IsFetching("Mine"));
    }
}

TEST_CASE("The registry caches each panel's last fetch and lists every known issue once", "[Tracker]") {
    RegistryResetGuard guard;
    tracker::AddPanel(Panel{.name = "Mine", .connection = "work"});
    tracker::AddPanel(Panel{.name = "Team", .connection = "work"});

    CHECK(tracker::KnownIssues().empty());
    tracker::SetPanelIssues("Team", {Issue{.key = "NED-2", .title = "team copy"}, Issue{.key = "NED-3"}});
    tracker::SetPanelIssues("Mine", {Issue{.key = "NED-1"}, Issue{.key = "NED-2", .title = "mine copy"}});
    // A panel that isn't declared contributes nothing.
    tracker::SetPanelIssues("Ghost", {Issue{.key = "NED-9"}});

    CHECK(tracker::PanelIssues("Mine").size() == 2);
    const std::vector<Issue> known = tracker::KnownIssues();
    REQUIRE(known.size() == 3);
    CHECK(known[0].key == "NED-1");
    CHECK(known[1] == Issue{.key = "NED-2", .title = "mine copy"});
    CHECK(known[2].key == "NED-3");

    tracker::RemovePanel("Mine");
    CHECK(tracker::PanelIssues("Mine").empty());
    CHECK(tracker::KnownIssues().size() == 2);
    tracker::ClearRegistry();
    CHECK(tracker::PanelIssues("Team").empty());
}

TEST_CASE("FinishView fills what the detail view left empty from the listed row", "[Tracker]") {
    const StubProvider provider;
    const Issue        listed{.key = "NED-1", .title = "Crash", .status = "Open", .labels = {"bug"}, .url = "https://x/NED-1"};

    SECTION("exit 0 merges") {
        const auto result = FinishView(provider, listed, "the body", 0);
        REQUIRE(result);
        CHECK(result->issue == Issue{.key = "NED-1", .title = "Crash", .status = "Done", .labels = {"bug"}, .url = "https://x/NED-1"});
        CHECK(result->body == "the body");
        CHECK(result->comments.size() == 1);
    }
    SECTION("failures name the issue") {
        const auto failed = FinishView(provider, listed, "HTTP 401\n", 22);
        REQUIRE_FALSE(failed);
        CHECK(failed.error() == "tracker issue \"NED-1\": fetch failed (exit 22): HTTP 401");
        const auto unparsed = FinishView(provider, listed, "garbage", 0);
        REQUIRE_FALSE(unparsed);
        CHECK(unparsed.error() == "tracker issue \"NED-1\": unparseable");
    }
}

TEST_CASE("Runner::RequestIssue fetches through the panel's connection", "[Tracker]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner             runner(eventLoop);
    const Issue        listed{.key = "NED-1", .title = "Crash"};

    std::optional<IssueDetail> detail;
    std::optional<std::string> error;
    const auto                 request = [&](const Issue& issue) {
        detail.reset();
        error.reset();
        runner.RequestIssue(
            "Mine", issue, [&](IssueDetail d) { detail = std::move(d); }, [&](std::string e) { error = std::move(e); });
    };

    SECTION("an unresolvable panel is reported") {
        request(listed);
        CHECK(error == "no tracker panel named \"Mine\"");
    }
    SECTION("a provider without a detail view completes at once with the listed row") {
        RegisterStubPanel(std::make_shared<StubProvider>(std::vector<std::string>{"sleep", "5"}, /*hasView=*/false));
        request(listed);
        REQUIRE(detail);
        CHECK(*detail == IssueDetail{.issue = listed});
    }
    SECTION("a detail view spawns, and a second request for the same issue is refused") {
        auto provider = std::make_shared<StubProvider>();
        RegisterStubPanel(provider);
        request(listed);
        CHECK_FALSE(detail);
        CHECK_FALSE(error);
        CHECK(provider->lastKey == "NED-1");
        CHECK(provider->lastConnection.name == "work");
        // Keyed apart from the panel's own list fetch.
        CHECK_FALSE(runner.IsFetching("Mine"));

        request(listed);
        CHECK(error == "tracker issue \"NED-1\" is already fetching");
        request(Issue{.key = "NED-2"});
        CHECK_FALSE(error);
    }
    SECTION("a throwing argv builder is reported") {
        RegisterStubPanel(std::make_shared<StubProvider>());
        request(Issue{.key = "THROW-1"});
        CHECK(error == "tracker issue \"THROW-1\": bad key");
    }
}

TEST_CASE("RenderIssue lays an issue out as Markdown", "[Tracker]") {
    const IssueDetail full{.issue    = Issue{.key      = "NED-7",
                                             .title    = "Rail tooltip clips",
                                             .status   = "In Progress",
                                             .assignee = "Ann",
                                             .labels   = {"ui", "bug"},
                                             .url      = "https://x/NED-7",
                                             .updated  = "2026-09-29"},
                           .body     = "It clips.\r\nOn resize.\r\n\r\n",
                           .comments = {Comment{.author = "Bo", .created = "2026-09-28", .body = "Seen it.\n"}, Comment{.body = "+1"}}};
    CHECK(tracker::RenderIssue(full) == "# NED-7: Rail tooltip clips\n"
                                        "\n"
                                        "- **Status:** In Progress\n"
                                        "- **Assignee:** Ann\n"
                                        "- **Labels:** ui, bug\n"
                                        "- **Updated:** 2026-09-29\n"
                                        "- **URL:** <https://x/NED-7>\n"
                                        "\n"
                                        "It clips.\n"
                                        "On resize.\n"
                                        "\n"
                                        "## Comments (2)\n"
                                        "\n"
                                        "### Bo — 2026-09-28\n"
                                        "\n"
                                        "Seen it.\n"
                                        "\n"
                                        "### (unknown)\n"
                                        "\n"
                                        "+1\n");

    CHECK(tracker::RenderIssue(IssueDetail{.issue = Issue{.key = "#42"}}) == "# #42\n\n\n_No description._\n");
}

TEST_CASE("ShowIssue rewrites one read-only buffer per issue", "[Tracker]") {
    ned::text::BufferList bufferList;
    ned::text::Buffer&    first = tracker::ShowIssue(bufferList, IssueDetail{.issue = Issue{.key = "NED-1"}, .body = "v1"});
    CHECK(first.Name() == "*issue NED-1*");
    CHECK(first.ReadOnly());
    CHECK(first.Text().find("v1") != std::string::npos);

    ned::text::Buffer& again = tracker::ShowIssue(bufferList, IssueDetail{.issue = Issue{.key = "NED-1"}, .body = "v2"});
    CHECK(&again == &first);
    CHECK(again.ReadOnly());
    CHECK(again.Text().find("v1") == std::string::npos);
    CHECK(again.Text().find("v2") != std::string::npos);
    CHECK(again.Point() == 0);
}

TEST_CASE("ParseRemoteUrls reads each URL of git remote -v once", "[Tracker]") {
    const std::string output = "origin\tgit@github.com:me/ned.git (fetch)\n"
                               "origin\tgit@github.com:me/ned.git (push)\n"
                               "upstream\thttps://github.com/org/ned (fetch)\n"
                               "upstream\thttps://github.com/org/ned (push)\n"
                               "local\t/srv/git/my repo (fetch)\n"
                               "garbage line\n";
    CHECK(tracker::ParseRemoteUrls(output) ==
          std::vector<std::string>{"git@github.com:me/ned.git", "https://github.com/org/ned", "/srv/git/my repo"});
    CHECK(tracker::ParseRemoteUrls("").empty());
}

TEST_CASE("AddDetectedPanels registers what providers find, never over configuration", "[Tracker]") {
    RegistryResetGuard guard;
    auto               provider = std::make_shared<StubProvider>();
    provider->detected          = {
        Detected{.connection = Connection{.name = "github.com/org/ned", .provider = "stub", .url = "https://github.com/org/ned"},
                 .panels     = {Panel{.name = "org/ned", .glyph = "G"}, Panel{.name = "Mine", .query = "involves:@me"}}},
        Detected{.connection = Connection{.name = "taken", .provider = "stub"}, .panels = {Panel{.name = "never"}}},
    };
    tracker::RegisterProvider("stub", provider);
    tracker::SetConnection(Connection{.name = "taken", .provider = "stub", .url = "mine"});
    tracker::AddPanel(Panel{.name = "Mine", .connection = "taken", .query = "declared"});

    const std::vector<std::string> remotes{"git@github.com:org/ned.git"};
    CHECK(tracker::AddDetectedPanels(remotes) == std::vector<std::string>{"org/ned"});
    CHECK(provider->lastRemotes == remotes);

    CHECK(tracker::FindConnection("github.com/org/ned")->url == "https://github.com/org/ned");
    CHECK(tracker::FindConnection("taken")->url == "mine");
    CHECK(tracker::FindPanel("org/ned") == Panel{.name = "org/ned", .connection = "github.com/org/ned", .glyph = "G"});
    CHECK(tracker::FindPanel("Mine")->query == "declared");
    CHECK_FALSE(tracker::FindPanel("never"));
}

TEST_CASE("AddDetectedPanels skips a throwing provider and an empty remote list", "[Tracker]") {
    RegistryResetGuard guard;
    auto               broken = std::make_shared<StubProvider>();
    broken->throwOnDetect     = true;
    auto working              = std::make_shared<StubProvider>();
    working->detected         = {Detected{.connection = Connection{.name = "c", .provider = "working"}, .panels = {Panel{.name = "p"}}}};
    tracker::RegisterProvider("broken", broken);
    tracker::RegisterProvider("working", working);

    CHECK(tracker::AddDetectedPanels({}).empty());
    CHECK(tracker::Panels().empty());
    CHECK(tracker::AddDetectedPanels({"url"}) == std::vector<std::string>{"p"});
}

TEST_CASE("Tracker auto-detect is on until turned off", "[Tracker]") {
    RegistryResetGuard guard;
    CHECK(tracker::AutoDetect());
    tracker::SetAutoDetect(false);
    CHECK_FALSE(tracker::AutoDetect());
    tracker::ClearRegistry();
    CHECK(tracker::AutoDetect());
}

TEST_CASE("SubstituteInputFile fills every {input-file} and insists on one", "[Tracker]") {
    const auto substituted = tracker::SubstituteInputFile({"curl", "--data-binary", "@{input-file}", "{input-file}"}, "/run/f");
    REQUIRE(substituted);
    CHECK(*substituted == std::vector<std::string>{"curl", "--data-binary", "@/run/f", "/run/f"});

    const auto missing = tracker::SubstituteInputFile({"curl", "-d", "@-"}, "/run/f");
    REQUIRE_FALSE(missing);
    CHECK(missing.error() == "the command has input but no {input-file} argument to read it from");
}

TEST_CASE("The completion halves of actions report failures with the command's output", "[Tracker]") {
    const ActingProvider provider;

    CHECK(tracker::FinishAction("DEV-1", "comment", "", 0));
    const auto failed = tracker::FinishAction("DEV-1", "comment", "{\"errorMessages\":[\"nope\"]}\n", 22);
    REQUIRE_FALSE(failed);
    CHECK(failed.error() == "tracker issue \"DEV-1\": comment failed (exit 22): {\"errorMessages\":[\"nope\"]}");

    const auto choices = tracker::FinishChoices(provider, Capability::Transition, "DEV-1", "Done", 0);
    REQUIRE(choices);
    CHECK(*choices == std::vector<Choice>{Choice{.id = "31", .name = "Done"}});
    CHECK(tracker::FinishChoices(provider, Capability::Transition, "DEV-1", "garbage", 0).error() == "tracker issue \"DEV-1\": unparseable");
    CHECK(tracker::FinishChoices(provider, Capability::Transition, "DEV-1", "", 1).error() ==
          "tracker issue \"DEV-1\": listing statuses failed (exit 1)");

    const auto keys = tracker::FinishProjectKeys(provider, "work", "DEV", 0);
    REQUIRE(keys);
    CHECK(*keys == std::vector<std::string>{"DEV"});
    CHECK(tracker::FinishProjectKeys(provider, "work", "", std::nullopt).error() ==
          "tracker connection \"work\": listing project keys failed (couldn't start the command)");
}

TEST_CASE("Runner actions resolve the connection and its provider's capability", "[Tracker]") {
    RegistryResetGuard guard;
    ned::ui::EventLoop eventLoop;
    Runner             runner(eventLoop);

    std::optional<std::string> error;
    bool                       done     = false;
    auto                       onDone   = [&done] { done = true; };
    auto                       onError  = [&error](std::string message) { error = std::move(message); };
    auto                       noChoice = [](std::vector<Choice>) {};

    SECTION("unknown connection") {
        runner.PostComment("work", "DEV-1", "hi", onDone, onError);
        CHECK(error == "no tracker connection named \"work\"");
    }
    SECTION("a provider without the capability") {
        RegisterStubPanel(std::make_shared<StubProvider>());
        runner.RequestTransitions("work", "DEV-1", noChoice, onError);
        CHECK(error == "tracker connection \"work\" can't change an issue's status");
        runner.PostWorklog("work", "DEV-1", tracker::Worklog{}, onDone, onError);
        CHECK(error == "tracker connection \"work\" can't log work");
    }
    SECTION("argv carries the issue and the choice, and one action per issue runs at a time") {
        auto provider = std::make_shared<ActingProvider>();
        RegisterStubPanel(provider);

        runner.Transition("work", "DEV-1", Choice{.id = "31", .name = "Done"}, onDone, onError);
        CHECK_FALSE(error);
        CHECK(provider->lastArgs == std::vector<std::string>{"transition", "DEV-1", "31"});

        runner.PostWorklog("work", "DEV-1", tracker::Worklog{.started = 100, .seconds = 60}, onDone, onError);
        CHECK(error == "tracker issue \"DEV-1\" is busy with a previous request");

        error.reset();
        runner.PostWorklog("work", "DEV-2", tracker::Worklog{.started = 100, .seconds = 60}, onDone, onError);
        CHECK_FALSE(error);
        CHECK(provider->lastArgs == std::vector<std::string>{"worklog", "DEV-2", "100", "60"});

        // Listing choices doesn't wait on an action.
        runner.RequestTransitions("work", "DEV-1", noChoice, onError);
        CHECK_FALSE(error);
        CHECK(provider->lastArgs == std::vector<std::string>{"transitions", "DEV-1"});
        CHECK_FALSE(done);
    }
    SECTION("the mine query goes through the provider's own list command") {
        auto provider = std::make_shared<ActingProvider>();
        RegisterStubPanel(provider);
        runner.RequestMine("work", [](std::vector<Issue>) {}, onError);
        CHECK_FALSE(error);
        CHECK(provider->lastQuery == "mine:work");
        runner.RequestMine("work", [](std::vector<Issue>) {}, onError);
        CHECK(error == "tracker connection \"work\" is busy with a previous request");
    }
}

TEST_CASE("A comment's body reaches its command through a private file removed afterwards", "[Tracker]") {
    RegistryResetGuard guard;
    RuntimeDir         runtime;
    auto               provider = std::make_shared<ActingProvider>();
    RegisterStubPanel(provider);

    std::optional<std::string> error;
    auto                       onError = [&error](std::string message) { error = std::move(message); };

    {
        ned::ui::EventLoop eventLoop;
        Runner             runner(eventLoop);
        runner.PostComment("work", "DEV-1", "Looks *good*", [] {}, onError);
        CHECK_FALSE(error);
        const std::vector<std::filesystem::path> files = runtime.Files();
        REQUIRE(files.size() == 1);
        std::ifstream      in(files[0]);
        std::ostringstream body;
        body << in.rdbuf();
        CHECK(body.str() == "Looks *good*");
    }
    CHECK(runtime.Files().empty());

    SECTION("a command with nowhere to read it from is refused, and leaves nothing behind") {
        provider->inputPlaceholder = false;
        ned::ui::EventLoop eventLoop;
        Runner             runner(eventLoop);
        runner.PostComment("work", "DEV-1", "hi", [] {}, onError);
        CHECK(error == "tracker issue \"DEV-1\": the command has input but no {input-file} argument to read it from");
        CHECK(runtime.Files().empty());
    }
}

TEST_CASE("ConnectionForIssue finds the connection of the first panel that listed the key", "[Tracker]") {
    RegistryResetGuard guard;
    tracker::AddPanel(Panel{.name = "Work", .connection = "work"});
    tracker::AddPanel(Panel{.name = "Home", .connection = "home"});
    tracker::SetPanelIssues("Home", {Issue{.key = "#4"}, Issue{.key = "DEV-1"}});
    tracker::SetPanelIssues("Work", {Issue{.key = "DEV-1"}});

    CHECK(tracker::ConnectionForIssue("DEV-1") == "work");
    CHECK(tracker::ConnectionForIssue("#4") == "home");
    CHECK_FALSE(tracker::ConnectionForIssue("DEV-2"));
}

TEST_CASE("ConnectionForAction wants a fetched issue on a capable connection", "[Tracker]") {
    RegistryResetGuard guard;
    CHECK(tracker::ConnectionForAction("DEV-1", Capability::Comment).error() == "no tracker panel has fetched DEV-1 -- open it from one first");

    RegisterStubPanel(std::make_shared<ActingProvider>());
    tracker::SetPanelIssues("Mine", {Issue{.key = "DEV-1"}});
    CHECK(tracker::ConnectionForAction("DEV-1", Capability::Comment) == "work");
    CHECK(tracker::ConnectionForAction("DEV-1", Capability::Assign).error() == "tracker connection \"work\" can't assign issues");
}

TEST_CASE("CommentText drops the blank edges of what was written", "[Tracker]") {
    CHECK(tracker::CommentText("\n\n  Looks good.\n\n- a\n  \n") == "  Looks good.\n\n- a");
    CHECK(tracker::CommentText(" \n\t\n").empty());
    CHECK(tracker::CommentText("").empty());
    CHECK(tracker::CommentBufferName("#4") == "*comment #4*");
}

TEST_CASE("Runner::RequestIssueOn fetches through a connection rather than a panel", "[Tracker]") {
    RegistryResetGuard guard;
    auto               provider = std::make_shared<StubProvider>();
    RegisterStubPanel(provider);
    ned::ui::EventLoop eventLoop;
    Runner             runner(eventLoop);

    std::optional<std::string> error;
    auto                       onError = [&error](std::string message) { error = std::move(message); };
    runner.RequestIssueOn("nope", Issue{.key = "NED-1"}, [](IssueDetail) {}, onError);
    CHECK(error == "no tracker connection named \"nope\"");

    error.reset();
    runner.RequestIssueOn("work", Issue{.key = "NED-1"}, [](IssueDetail) {}, onError);
    CHECK_FALSE(error);
    CHECK(provider->lastKey == "NED-1");
    // The same run key as a panel's fetch of that issue.
    runner.RequestIssue("Mine", Issue{.key = "NED-1"}, [](IssueDetail) {}, onError);
    CHECK(error == "tracker issue \"NED-1\" is already fetching");
}

TEST_CASE("PutOwnUserFirst moves the connection's user to the front", "[Tracker]") {
    std::vector<Choice> users{{.id = "1", .name = "Ann", .email = "ann@x.io"}, {.id = "2", .name = "Me", .email = "ME@x.io"}, {.id = "3", .name = "Bo"}};
    tracker::PutOwnUserFirst(users, "me@x.io");
    CHECK(users == std::vector<Choice>{{.id = "2", .name = "Me (me)", .email = "ME@x.io"}, {.id = "1", .name = "Ann", .email = "ann@x.io"}, {.id = "3", .name = "Bo"}});

    std::vector<Choice> unchanged{{.id = "1", .name = "Ann"}};
    tracker::PutOwnUserFirst(unchanged, "");
    tracker::PutOwnUserFirst(unchanged, "me@x.io");
    CHECK(unchanged == std::vector<Choice>{{.id = "1", .name = "Ann"}});
}
