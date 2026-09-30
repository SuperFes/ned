#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Editor/Tracker/KeyCompletion.h"
#include "Editor/Tracker/Registry.h"
#include "Editor/Tracker/Runner.h"
#include "UI/EventLoop.h"

using ned::editor::tracker::Connection;
using ned::editor::tracker::Issue;
using ned::editor::tracker::IssueKeyAt;
using ned::editor::tracker::IssueKeyPrefixStart;
using ned::editor::tracker::Panel;

namespace tracker = ned::editor::tracker;

namespace {

// Counts the fetches RefreshKeyCaches starts; numeric when asked.
class KeyedProvider : public tracker::Provider {
  public:
    explicit KeyedProvider(bool numeric = false) : numeric_(numeric) {
    }
    [[nodiscard]] tracker::CommandSpec ListArgv(const Connection&, const std::string& query) const override {
        lastQuery = query;
        ++mineFetches;
        return tracker::CommandSpec{.argv = {"sleep", "5"}};
    }
    [[nodiscard]] std::vector<Issue> ParseList(const std::string&) const override {
        return {};
    }
    [[nodiscard]] std::optional<tracker::CommandSpec> ViewArgv(const Connection&, const std::string&) const override {
        return std::nullopt;
    }
    [[nodiscard]] tracker::IssueDetail ParseView(const std::string&) const override {
        return {};
    }
    [[nodiscard]] std::vector<tracker::Detected> Detect(const std::vector<std::string>&) const override {
        return {};
    }
    [[nodiscard]] bool Supports(tracker::Capability capability) const override {
        return capability == tracker::Capability::Mine || (!numeric_ && capability == tracker::Capability::ProjectKeys);
    }
    [[nodiscard]] bool NumericKeys() const override {
        return numeric_;
    }
    [[nodiscard]] tracker::CommandSpec ProjectKeysArgv(const Connection&) const override {
        ++keyFetches;
        return tracker::CommandSpec{.argv = {"sleep", "5"}};
    }
    [[nodiscard]] std::string MineQuery(const Connection&) const override {
        return "mine";
    }

    mutable int         mineFetches = 0;
    mutable int         keyFetches  = 0;
    mutable std::string lastQuery;

  private:
    bool numeric_;
};

struct RegistryGuard {
    RegistryGuard() {
        tracker::ClearRegistry();
        tracker::ResetKeyCacheAttempts();
    }
    ~RegistryGuard() {
        tracker::ClearRegistry();
        tracker::ResetKeyCacheAttempts();
    }
};

} // namespace

TEST_CASE("IssueKeyPrefixStart finds a key being typed at the end of the text", "[TrackerKeyCompletion]") {
    const std::set<std::string> keys{"DEV", "ENDEV_2"};
    CHECK(IssueKeyPrefixStart("Fixes DEV-", keys, false) == 6);
    CHECK(IssueKeyPrefixStart("Fixes DEV-45", keys, false) == 6);
    CHECK(IssueKeyPrefixStart("(ENDEV_2-1", keys, false) == 1);
    CHECK(IssueKeyPrefixStart("DEV-4", keys, false) == 0);
    CHECK_FALSE(IssueKeyPrefixStart("Fixes OPS-4", keys, false)); // not a known project
    CHECK_FALSE(IssueKeyPrefixStart("xDEV-4", keys, false));      // glued to a word
    CHECK_FALSE(IssueKeyPrefixStart("dev-4", keys, false));
    CHECK_FALSE(IssueKeyPrefixStart("DEV-4 ", keys, false)); // finished
    CHECK_FALSE(IssueKeyPrefixStart("DEV", keys, false));
    CHECK_FALSE(IssueKeyPrefixStart("", keys, false));

    CHECK(IssueKeyPrefixStart("Refs #", keys, true) == 5);
    CHECK(IssueKeyPrefixStart("#12", keys, true) == 0);
    CHECK_FALSE(IssueKeyPrefixStart("Refs #", keys, false));
    CHECK_FALSE(IssueKeyPrefixStart("## ", keys, true));
    CHECK_FALSE(IssueKeyPrefixStart("##", keys, true));   // a Markdown heading
    CHECK_FALSE(IssueKeyPrefixStart("&#12", keys, true)); // an HTML entity
    CHECK_FALSE(IssueKeyPrefixStart("a#12", keys, true));
}

TEST_CASE("IssueKeyCompletions offers the project's issues in the order given", "[TrackerKeyCompletion]") {
    const std::vector<Issue> issues{{.key = "DEV-9", .title = "Newest", .status = "Open"}, {.key = "OPS-1"}, {.key = "DEV-10", .title = "Older"}, {.key = "#4", .title = "Numeric"}};
    const auto               dev = tracker::IssueKeyCompletions(issues, "DEV-1");
    REQUIRE(dev.size() == 2);
    CHECK(dev[0].label == "DEV-9");
    CHECK(dev[0].insertText == "DEV-9");
    CHECK(dev[0].detail == "Newest");
    CHECK(dev[0].documentation == "Newest\n\nStatus: Open");
    CHECK(dev[0].source == ned::editor::CompletionSource::IssueKey);
    CHECK(dev[1].label == "DEV-10");
    CHECK(dev[0].sortText < dev[1].sortText);

    // A long title is cut so the key beside it stays readable.
    const auto longTitle = tracker::IssueKeyCompletions({{.key = "DEV-1", .title = std::string(38, 'x') + "ééé"}}, "DEV-");
    CHECK(longTitle[0].detail == std::string(38, 'x') + "éé…");

    const auto numeric = tracker::IssueKeyCompletions(issues, "#");
    REQUIRE(numeric.size() == 1);
    CHECK(numeric[0].label == "#4");
}

TEST_CASE("IssueKeyAt finds the whole key under a column", "[TrackerKeyCompletion]") {
    const std::set<std::string> keys{"DEV"};
    const std::string           line = "See DEV-450, and #12.";
    for (std::size_t column = 4; column <= 11; ++column) {
        const auto found = IssueKeyAt(line, column, keys, true);
        REQUIRE(found);
        CHECK(found->key == "DEV-450");
        CHECK(found->start == 4);
        CHECK(found->end == 11);
    }
    CHECK_FALSE(IssueKeyAt(line, 2, keys, true));
    CHECK(IssueKeyAt(line, 18, keys, true)->key == "#12");
    CHECK_FALSE(IssueKeyAt(line, 18, keys, false));
    CHECK_FALSE(IssueKeyAt("OPS-1", 1, keys, true));
    CHECK_FALSE(IssueKeyAt("DEV-1x", 1, keys, true));
}

TEST_CASE("The registry's own-issue and project-key caches find connections", "[TrackerKeyCompletion]") {
    RegistryGuard guard;
    tracker::AddPanel(Panel{.name = "Mine", .connection = "work"});
    tracker::SetPanelIssues("Mine", {Issue{.key = "OPS-3"}});
    tracker::SetMineIssues("work", {Issue{.key = "DEV-1"}, Issue{.key = "#4"}});
    tracker::SetMineIssues("home", {Issue{.key = "DEV-1"}, Issue{.key = "HOME-2"}});
    tracker::SetProjectKeys("work", {"DEV", "QA"});

    // By connection name, first occurrence wins.
    CHECK(tracker::MineIssues() == std::vector<Issue>{{.key = "DEV-1"}, {.key = "HOME-2"}, {.key = "#4"}});
    CHECK(tracker::KnownProjectKeys() == std::set<std::string>{"DEV", "HOME", "OPS", "QA"});
    CHECK(tracker::ConnectionForProjectKey("QA") == "work");
    CHECK(tracker::ConnectionForProjectKey("HOME") == "home");
    CHECK(tracker::ConnectionForProjectKey("OPS") == "work");
    CHECK_FALSE(tracker::ConnectionForProjectKey("NOPE"));
    CHECK(tracker::ConnectionForIssue("HOME-2") == "home");

    CHECK(tracker::ConnectionForKey("QA-99") == "work");
    CHECK_FALSE(tracker::ConnectionForKey("#99"));
    tracker::RegisterProvider("numeric", std::make_shared<KeyedProvider>(true));
    tracker::SetConnection(Connection{.name = "gh", .provider = "numeric"});
    CHECK(tracker::ConnectionForKey("#99") == "gh");
    CHECK(tracker::ConnectionForKey("#4") == "work");
}

TEST_CASE("RefreshKeyCaches fetches each connection's caches once per lifetime", "[TrackerKeyCompletion]") {
    RegistryGuard guard;
    auto          jira = std::make_shared<KeyedProvider>();
    auto          gh   = std::make_shared<KeyedProvider>(true);
    tracker::RegisterProvider("jira", jira);
    tracker::RegisterProvider("gh", gh);
    tracker::SetConnection(Connection{.name = "work", .provider = "jira"});
    tracker::SetConnection(Connection{.name = "repo", .provider = "gh"});

    ned::ui::EventLoop eventLoop;
    tracker::Runner    runner(eventLoop);
    const auto         start = std::chrono::steady_clock::now();

    tracker::RefreshKeyCaches(runner, start);
    CHECK(jira->keyFetches == 1);
    CHECK(jira->mineFetches == 1);
    CHECK(jira->lastQuery == "mine");
    CHECK(gh->keyFetches == 0); // it has no project keys
    CHECK(gh->mineFetches == 1);

    tracker::RefreshKeyCaches(runner, start + std::chrono::minutes(4));
    CHECK(jira->mineFetches == 1);

    // Past the lifetime it tries again; the first is still running, which
    // the Runner refuses rather than running twice.
    tracker::RefreshKeyCaches(runner, start + tracker::kKeyCacheLifetime);
    CHECK(jira->mineFetches == 1);
    CHECK(jira->keyFetches == 1);
}
