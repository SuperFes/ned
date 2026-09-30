#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/Tracker/Registry.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/PluginLoader.h"
#include "JanetTestSupport.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::tracker::Comment;
using ned::editor::tracker::Connection;
using ned::editor::tracker::Detected;
using ned::editor::tracker::Issue;
using ned::editor::tracker::IssueDetail;
using ned::editor::tracker::Panel;
using ned::editor::tracker::Provider;

namespace tracker = ned::editor::tracker;

namespace {

struct RegistryResetGuard {
    RegistryResetGuard() {
        tracker::ClearRegistry();
    }
    ~RegistryResetGuard() {
        tracker::ClearRegistry();
    }
};

// A PATH holding only a fake `gh` (or nothing), so detection doesn't depend
// on what the machine running the tests has installed.
class FakePath {
  public:
    explicit FakePath(bool withGh) {
        std::string pattern = (std::filesystem::temp_directory_path() / "ned_tracker_gh_XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        dir_ = pattern;
        if (withGh) {
            const std::filesystem::path gh = dir_ / "gh";
            std::ofstream(gh) << "#!/bin/sh\n";
            std::filesystem::permissions(gh, std::filesystem::perms::owner_all);
        }
        if (const char* existing = std::getenv("PATH")) {
            previous_ = existing;
        }
        ::setenv("PATH", dir_.c_str(), 1);
    }

    ~FakePath() {
        ::setenv("PATH", previous_.c_str(), 1);
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
    }

    FakePath(const FakePath&)            = delete;
    FakePath& operator=(const FakePath&) = delete;

  private:
    std::filesystem::path dir_;
    std::string           previous_;
};

std::shared_ptr<const Provider> LoadGithubProvider() {
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);
    env.DoString(ned::janet::ReadBundledPlugin("tracker-github"), "tracker-github.janet");
    auto provider = tracker::FindProvider("github");
    REQUIRE(provider);
    return provider;
}

const Connection kRepo{.name = "github.com/cli/cli", .provider = "github", .url = "https://github.com/cli/cli"};

} // namespace

TEST_CASE("tracker-github lists through gh issue list", "[TrackerGithub]") {
    RegistryResetGuard guard;
    const auto         github = LoadGithubProvider();

    const std::vector<std::string> base{"gh", "issue", "list", "--repo", "github.com/cli/cli", "--limit", "100", "--json",
                                        "number,title,state,assignees,labels,url,updatedAt"};
    CHECK(github->ListArgv(kRepo, "").argv == base);

    std::vector<std::string> searched = base;
    searched.insert(searched.end(), {"--search", "is:closed label:bug"});
    CHECK(github->ListArgv(kRepo, "is:closed label:bug").argv == searched);

    // Trailing slash and .git are tolerated, a bare OWNER/REPO passes through.
    CHECK(github->ListArgv(Connection{.url = "https://github.com/cli/cli.git/"}, "").argv[4] == "github.com/cli/cli");
    CHECK(github->ListArgv(Connection{.url = "cli/cli"}, "").argv[4] == "cli/cli");
    CHECK_THROWS_WITH(github->ListArgv(Connection{.name = "bare"}, ""), ContainsSubstring("\"bare\" needs a :url"));

    const std::vector<Issue> issues = github->ParseList(R"([
        {"number": 7, "title": "Rail tooltip clips", "state": "OPEN", "url": "https://github.com/cli/cli/issues/7",
         "updatedAt": "2026-09-28T12:00:00Z", "assignees": [{"login": "ann"}, {"login": "bo"}],
         "labels": [{"name": "ui"}, {"name": "bug"}]},
        {"number": 8, "title": "Done", "state": "CLOSED", "assignees": [], "labels": []}
    ])");
    REQUIRE(issues.size() == 2);
    CHECK(issues[0] == Issue{.key      = "#7",
                             .title    = "Rail tooltip clips",
                             .status   = "Open",
                             .assignee = "ann, bo",
                             .labels   = {"ui", "bug"},
                             .url      = "https://github.com/cli/cli/issues/7",
                             .updated  = "2026-09-28T12:00:00Z"});
    CHECK(issues[1].status == "Closed");
    CHECK(issues[1].assignee.empty());
}

TEST_CASE("tracker-github views one issue with its comments", "[TrackerGithub]") {
    RegistryResetGuard guard;
    const auto         github = LoadGithubProvider();

    const auto argv = github->ViewArgv(kRepo, "#42");
    REQUIRE(argv);
    CHECK(argv->argv == std::vector<std::string>{"gh", "issue", "view", "42", "--repo", "github.com/cli/cli", "--json",
                                                 "number,title,state,assignees,labels,url,updatedAt,body,comments"});

    const IssueDetail detail = github->ParseView(R"({"number": 42, "title": "Crash", "state": "OPEN", "body": "Steps",
        "assignees": [], "labels": [],
        "comments": [{"author": {"login": "ann"}, "createdAt": "2026-09-28T12:00:00Z", "body": "Same here"},
                     {"author": null, "createdAt": "2026-09-29T08:00:00Z", "body": "Ghost"}]})");
    CHECK(detail.issue.key == "#42");
    CHECK(detail.issue.status == "Open");
    CHECK(detail.body == "Steps");
    CHECK(detail.comments == std::vector<Comment>{{.author = "ann", .created = "2026-09-28T12:00:00Z", .body = "Same here"},
                                                  {.author = "", .created = "2026-09-29T08:00:00Z", .body = "Ghost"}});
}

TEST_CASE("tracker-github detects github.com remotes when gh is installed", "[TrackerGithub]") {
    RegistryResetGuard guard;
    const auto         github = LoadGithubProvider();

    const std::vector<std::string> remotes{
        "git@github.com:me/ned.git",
        "https://github.com/me/ned", // the same repository
        "ssh://git@github.com:22/Org/Ned.git",
        "https://token@github.com/org/ned.git/", // Org/Ned again: GitHub names are case-insensitive
        "org-123@github.com:corp/tool",
        "https://gitlab.com/me/ned.git",
        "https://github.com.example/me/ned",
        "/srv/git/ned",
    };

    SECTION("with gh") {
        FakePath                    path(true);
        const std::vector<Detected> found = github->Detect(remotes);
        REQUIRE(found.size() == 3);
        CHECK(found[0].connection ==
              Connection{.name = "github.com/me/ned", .provider = "github", .url = "https://github.com/me/ned"});
        CHECK(found[0].panels == std::vector<Panel>{{.name = "me/ned", .connection = "github.com/me/ned", .glyph = ""}});
        CHECK(found[1].connection.name == "github.com/Org/Ned");
        CHECK(found[2].connection.url == "https://github.com/corp/tool");
    }

    SECTION("without gh") {
        FakePath path(false);
        CHECK(github->Detect(remotes).empty());
    }
}
