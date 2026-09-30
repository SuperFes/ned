#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/Tracker/IssueBuffer.h"
#include "Editor/Tracker/Registry.h"
#include "Editor/Tracker/Runner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::tracker::CommandSpec;
using ned::editor::tracker::Comment;
using ned::editor::tracker::Connection;
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
        return CommandSpec{argv_};
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

    mutable Connection  lastConnection;
    mutable std::string lastQuery;
    mutable std::string lastKey;

  private:
    std::vector<std::string> argv_;
    bool                     hasView_ = true;
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
