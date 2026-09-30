#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/Tracker/Registry.h"
#include "Editor/Tracker/Runner.h"
#include "UI/EventLoop.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::tracker::CommandSpec;
using ned::editor::tracker::Connection;
using ned::editor::tracker::FinishList;
using ned::editor::tracker::Issue;
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
    explicit StubProvider(std::vector<std::string> argv = {"sleep", "5"}) : argv_(std::move(argv)) {
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

    mutable Connection  lastConnection;
    mutable std::string lastQuery;

  private:
    std::vector<std::string> argv_;
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
