#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <set>
#include <string>

#include "Editor/Project/Root.h"
#include "Editor/Tracker/IssueBuffer.h"
#include "Editor/Tracker/IssueKey.h"
#include "Editor/Tracker/Registry.h"
#include "Editor/Vcs/CurrentBranch.h"

using ned::editor::tracker::Connection;
using ned::editor::tracker::Issue;
using ned::editor::tracker::IssueKeyFromBranch;
using ned::editor::tracker::Panel;

namespace tracker = ned::editor::tracker;

namespace {

class NumericProvider : public tracker::Provider {
  public:
    [[nodiscard]] tracker::CommandSpec ListArgv(const Connection&, const std::string&) const override {
        return {};
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
    [[nodiscard]] bool NumericKeys() const override {
        return true;
    }
};

struct StateGuard {
    std::filesystem::path previousRoot = ned::editor::ProjectRoot();
    StateGuard() {
        tracker::ClearRegistry();
        ned::editor::vcs::ClearCurrentBranches();
        tracker::SetCommitSeeds(std::string(tracker::kDefaultCommitSeed), std::string(tracker::kDefaultNumericCommitSeed));
        ned::editor::SetProjectRoot("/repo");
    }
    ~StateGuard() {
        tracker::ClearRegistry();
        ned::editor::vcs::ClearCurrentBranches();
        tracker::SetCommitSeeds(std::string(tracker::kDefaultCommitSeed), std::string(tracker::kDefaultNumericCommitSeed));
        ned::editor::SetProjectRoot(previousRoot);
    }
};

} // namespace

TEST_CASE("IssueKeyFromBranch finds a PROJ-123 key anywhere, delimited", "[TrackerIssueKey]") {
    const std::set<std::string> none;
    CHECK(IssueKeyFromBranch("DEV-450", none, false) == "DEV-450");
    CHECK(IssueKeyFromBranch("feature/DEV-450-fix-login", none, false) == "DEV-450");
    CHECK(IssueKeyFromBranch("aaron/ENDEV_2-17_hotfix", none, false) == "ENDEV_2-17");
    CHECK_FALSE(IssueKeyFromBranch("main", none, false));
    // Lower case needs the project to be known, so release-2 isn't RELEASE-2.
    CHECK_FALSE(IssueKeyFromBranch("feature/dev-450-fix", none, false));
    CHECK(IssueKeyFromBranch("feature/dev-450-fix", {"DEV"}, false) == "DEV-450");
    CHECK_FALSE(IssueKeyFromBranch("release-2", {"DEV"}, false));
    // Delimited on both sides by anything but a letter or digit.
    CHECK_FALSE(IssueKeyFromBranch("xDEV-450", none, false));
    CHECK_FALSE(IssueKeyFromBranch("DEV-450a", none, false));
    CHECK_FALSE(IssueKeyFromBranch("DEV-", none, false));
    // One letter isn't a project key without the project's say-so.
    CHECK_FALSE(IssueKeyFromBranch("v-2", none, false));
    CHECK(IssueKeyFromBranch("A-2", {"A"}, false) == "A-2");
}

TEST_CASE("IssueKeyFromBranch reads a leading issue number only for numeric trackers", "[TrackerIssueKey]") {
    const std::set<std::string> none;
    CHECK(IssueKeyFromBranch("42-fix-login", none, true) == "#42");
    CHECK(IssueKeyFromBranch("fix/42", none, true) == "#42");
    CHECK(IssueKeyFromBranch("issue-42", none, true) == "#42");
    CHECK(IssueKeyFromBranch("user/Issues_7-thing", none, true) == "#7");
    CHECK_FALSE(IssueKeyFromBranch("42-fix-login", none, false));
    CHECK_FALSE(IssueKeyFromBranch("42fix", none, true));
    CHECK_FALSE(IssueKeyFromBranch("fix-42", none, true));
    // A project key wins over a number.
    CHECK(IssueKeyFromBranch("12-DEV-3", none, true) == "DEV-3");
}

TEST_CASE("BranchNameForIssue", "[TrackerIssueKey]") {
    CHECK(tracker::BranchNameForIssue(Issue{.key = "DEV-450", .title = "Fix the login crash!"}) == "DEV-450-fix-the-login-crash");
    CHECK(tracker::BranchNameForIssue(Issue{.key = "#42", .title = "  Tooltip: clips (again)  "}) == "42-tooltip-clips-again");
    CHECK(tracker::BranchNameForIssue(Issue{.key = "DEV-1"}) == "DEV-1");
    CHECK(tracker::BranchNameForIssue(Issue{.key = "DEV-1", .title = "日本語"}) == "DEV-1");
    // Cut at a word boundary once the title part passes 40 characters.
    CHECK(tracker::BranchNameForIssue(Issue{.key = "DEV-2", .title = "one two three four five six seven eight nine ten eleven"}) ==
          "DEV-2-one-two-three-four-five-six-seven-eight");
    CHECK(tracker::BranchNameForIssue(Issue{.key = "DEV-3", .title = std::string(60, 'a')}) == "DEV-3-" + std::string(40, 'a'));
}

TEST_CASE("CommitSeed fills {key} into the template for the key's style", "[TrackerIssueKey]") {
    StateGuard guard;
    CHECK(tracker::CommitSeed("DEV-450") == "DEV-450 ");
    CHECK(tracker::CommitSeed("#42") == "\n\nRefs #42");
    tracker::SetCommitSeeds("[{key}] ", "");
    CHECK(tracker::CommitSeed("DEV-450") == "[DEV-450] ");
    CHECK(tracker::CommitSeed("#42").empty());
}

TEST_CASE("IssueKeyOfBufferName inverts IssueBufferName", "[TrackerIssueKey]") {
    CHECK(tracker::IssueKeyOfBufferName(tracker::IssueBufferName("DEV-450")) == "DEV-450");
    CHECK(tracker::IssueKeyOfBufferName(tracker::IssueBufferName("#42")) == "#42");
    CHECK_FALSE(tracker::IssueKeyOfBufferName("*issue *"));
    CHECK_FALSE(tracker::IssueKeyOfBufferName("*scratch*"));
}

TEST_CASE("CurrentIssueKey reads the recorded branch against the registry", "[TrackerIssueKey]") {
    StateGuard guard;
    CHECK_FALSE(tracker::CurrentIssueKey());

    ned::editor::vcs::SetCurrentBranch("/repo", "feature/dev-450-login");
    // Nothing is shown until a tracker is configured at all.
    CHECK_FALSE(tracker::CurrentIssueKey());

    tracker::SetConnection(Connection{.name = "work", .provider = "jira"});
    CHECK_FALSE(tracker::CurrentIssueKey()); // lower case, and DEV isn't known yet

    tracker::AddPanel(Panel{.name = "Mine", .connection = "work"});
    tracker::SetPanelIssues("Mine", {Issue{.key = "DEV-1"}});
    CHECK(tracker::CurrentIssueKey() == "DEV-450");

    ned::editor::vcs::SetCurrentBranch("/repo", "42-tooltip");
    CHECK_FALSE(tracker::CurrentIssueKey());
    tracker::RegisterProvider("numeric", std::make_shared<NumericProvider>());
    tracker::SetConnection(Connection{.name = "gh", .provider = "numeric"});
    CHECK(tracker::CurrentIssueKey() == "#42");

    // Another project's branch isn't this one's.
    ned::editor::SetProjectRoot("/elsewhere");
    CHECK_FALSE(tracker::CurrentIssueKey());
}
