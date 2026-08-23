#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <string>

#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "JanetTestSupport.h"

using ned::janet::Environment;
using ned::janet::InstallEditorBindings;

namespace {

struct RegistryResetGuard {
    RegistryResetGuard() {
        ned::editor::vcs::ClearRegistry();
    }
    ~RegistryResetGuard() {
        ned::editor::vcs::ClearRegistry();
    }
};

} // namespace

TEST_CASE("ned/vcs-register-provider registers a provider resolvable via ActiveProviderFor", "[JanetVcsProvider]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/vcs-register-provider "fake"
        {:detect (fn [root] (= root "/repo"))
         :blame-argv (fn [path] ["fake-vcs" "blame" path])
         :parse-blame (fn [stdout] [{:hash "abc123" :author "Ada" :date "2026-01-01" :summary "did a thing"}])
         :log-argv (fn [path] ["fake-vcs" "log" path])
         :parse-log (fn [stdout] [{:hash "def456" :author "Bea" :date "2026-01-02" :summary "did another thing"}])
         :diff-argv (fn [path] ["fake-vcs" "diff" path])
         :parse-diff (fn [stdout] [{:old-start 2 :old-count 1 :new-start 2 :new-count 3}])
         :working-diff-argv (fn [root] ["fake-vcs" "working-diff" root])
         :status-argv (fn [root] ["fake-vcs" "status" root])
         :parse-status (fn [stdout] [{:state "??" :path "new.txt"}])
         :stage-argv (fn [path] ["fake-vcs" "stage" path])
         :unstage-argv (fn [path] ["fake-vcs" "unstage" path])
         :commit-argv (fn [root message] ["fake-vcs" "commit" root message])
         :branch-list-argv (fn [root] ["fake-vcs" "branches" root])
         :parse-branch-list (fn [stdout] [{:name "main" :current true} {:name "dev" :current false}])
         :branch-switch-argv (fn [root name] ["fake-vcs" "switch" root name])
         :branch-create-argv (fn [root name] ["fake-vcs" "create" root name])})
    )");

    auto* provider = ned::editor::vcs::ActiveProviderFor("/repo");
    REQUIRE(provider != nullptr);
    REQUIRE(ned::editor::vcs::ActiveProviderFor("/not-repo") == nullptr);

    const auto blameArgv = provider->BlameArgv("/repo/file.txt");
    REQUIRE(blameArgv.argv == std::vector<std::string>{"fake-vcs", "blame", "/repo/file.txt"});

    const auto blameLines = provider->ParseBlame("irrelevant raw output");
    REQUIRE(blameLines.size() == 1);
    REQUIRE(blameLines[0].commitHash == "abc123");
    REQUIRE(blameLines[0].author == "Ada");
    REQUIRE(blameLines[0].date == "2026-01-01");
    REQUIRE(blameLines[0].summary == "did a thing");

    const auto logArgv = provider->LogArgv("/repo/file.txt");
    REQUIRE(logArgv.argv == std::vector<std::string>{"fake-vcs", "log", "/repo/file.txt"});

    const auto logEntries = provider->ParseLog("irrelevant raw output");
    REQUIRE(logEntries.size() == 1);
    REQUIRE(logEntries[0].commitHash == "def456");
    REQUIRE(logEntries[0].author == "Bea");

    const auto diffArgv = provider->DiffArgv("/repo/file.txt");
    REQUIRE(diffArgv.argv == std::vector<std::string>{"fake-vcs", "diff", "/repo/file.txt"});

    const auto hunks = provider->ParseDiff("irrelevant raw output");
    REQUIRE(hunks.size() == 1);
    REQUIRE(hunks[0].oldStart == 2);
    REQUIRE(hunks[0].oldCount == 1);
    REQUIRE(hunks[0].newStart == 2);
    REQUIRE(hunks[0].newCount == 3);

    const auto workingDiffArgv = provider->WorkingDiffArgv("/repo");
    REQUIRE(workingDiffArgv.argv == std::vector<std::string>{"fake-vcs", "working-diff", "/repo"});

    // The vocabulary-completion operations, including both two-string-arg
    // shapes (commit's root+message, branch-switch/-create's root+name).
    REQUIRE(provider->StatusArgv("/repo").argv == std::vector<std::string>{"fake-vcs", "status", "/repo"});

    const auto statusEntries = provider->ParseStatus("irrelevant raw output");
    REQUIRE(statusEntries.size() == 1);
    REQUIRE(statusEntries[0].state == "??");
    REQUIRE(statusEntries[0].path == "new.txt");

    REQUIRE(provider->StageArgv("/repo/a.txt").argv == std::vector<std::string>{"fake-vcs", "stage", "/repo/a.txt"});
    REQUIRE(provider->UnstageArgv("/repo/a.txt").argv == std::vector<std::string>{"fake-vcs", "unstage", "/repo/a.txt"});

    REQUIRE(provider->CommitArgv("/repo", "a \"quoted\" message").argv ==
            std::vector<std::string>{"fake-vcs", "commit", "/repo", "a \"quoted\" message"});

    REQUIRE(provider->BranchListArgv("/repo").argv == std::vector<std::string>{"fake-vcs", "branches", "/repo"});

    const auto branches = provider->ParseBranchList("irrelevant raw output");
    REQUIRE(branches.size() == 2);
    REQUIRE(branches[0].name == "main");
    REQUIRE(branches[0].current);
    REQUIRE(branches[1].name == "dev");
    REQUIRE_FALSE(branches[1].current);

    REQUIRE(provider->BranchSwitchArgv("/repo", "dev").argv == std::vector<std::string>{"fake-vcs", "switch", "/repo", "dev"});
    REQUIRE(provider->BranchCreateArgv("/repo", "feature").argv ==
            std::vector<std::string>{"fake-vcs", "create", "/repo", "feature"});
}

TEST_CASE("ned/vcs-register-provider's parse callbacks degrade missing fields to empty/zero, not a crash",
          "[JanetVcsProvider]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/vcs-register-provider "sparse"
        {:detect (fn [root] true)
         :blame-argv (fn [path] ["true"])
         :parse-blame (fn [stdout] [{:hash "abc123"}])
         :log-argv (fn [path] ["true"])
         :parse-log (fn [stdout] [])
         :diff-argv (fn [path] ["true"])
         :parse-diff (fn [stdout] [{:old-start 4}])
         :parse-status (fn [stdout] [{:state " M"}])
         :parse-branch-list (fn [stdout] [{:name "main"}])})
    )");

    auto* provider = ned::editor::vcs::ActiveProviderFor("/anything");
    REQUIRE(provider != nullptr);

    const auto blameLines = provider->ParseBlame("x");
    REQUIRE(blameLines.size() == 1);
    REQUIRE(blameLines[0].commitHash == "abc123");
    REQUIRE(blameLines[0].author.empty());
    REQUIRE(blameLines[0].date.empty());
    REQUIRE(blameLines[0].summary.empty());

    REQUIRE(provider->ParseLog("x").empty());

    const auto hunks = provider->ParseDiff("x");
    REQUIRE(hunks.size() == 1);
    REQUIRE(hunks[0].oldStart == 4);
    REQUIRE(hunks[0].oldCount == 0);
    REQUIRE(hunks[0].newStart == 0);
    REQUIRE(hunks[0].newCount == 0);

    const auto statusEntries = provider->ParseStatus("x");
    REQUIRE(statusEntries.size() == 1);
    REQUIRE(statusEntries[0].state == " M");
    REQUIRE(statusEntries[0].path.empty());

    const auto branches = provider->ParseBranchList("x");
    REQUIRE(branches.size() == 1);
    REQUIRE(branches[0].name == "main");
    REQUIRE_FALSE(branches[0].current); // absent :current degrades to false
}

TEST_CASE("ned/vcs-register-provider re-registering the same name replaces the previous provider",
          "[JanetVcsProvider]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/vcs-register-provider "replaceable"
        {:detect (fn [root] true) :blame-argv (fn [path] ["v1"])})
    )");
    env.DoString(R"(
      (ned/vcs-register-provider "replaceable"
        {:detect (fn [root] true) :blame-argv (fn [path] ["v2"])})
    )");

    auto* provider = ned::editor::vcs::ActiveProviderFor("/anything");
    REQUIRE(provider != nullptr);
    REQUIRE(provider->BlameArgv("x").argv == std::vector<std::string>{"v2"});
}

TEST_CASE("a provider registered without an operation's callbacks reports it as unsupported", "[JanetVcsProvider]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    // Only :detect and one operation -- everything else must throw the
    // VcsProvider-default "not supported by this provider" error rather
    // than crash or silently succeed, which is what VcsRunner's onError
    // path turns into a status-line message.
    env.DoString(R"(
      (ned/vcs-register-provider "blame-only"
        {:detect (fn [root] true)
         :blame-argv (fn [path] ["true"])
         :parse-blame (fn [stdout] [])})
    )");

    auto* provider = ned::editor::vcs::ActiveProviderFor("/anything");
    REQUIRE(provider != nullptr);
    REQUIRE_NOTHROW(provider->BlameArgv("x"));
    REQUIRE_THROWS_WITH(provider->StatusArgv("/root"), "status not supported by this provider");
    REQUIRE_THROWS_WITH(provider->ParseStatus("x"), "status not supported by this provider");
    REQUIRE_THROWS_WITH(provider->StageArgv("x"), "stage not supported by this provider");
    REQUIRE_THROWS_WITH(provider->UnstageArgv("x"), "unstage not supported by this provider");
    REQUIRE_THROWS_WITH(provider->CommitArgv("/root", "msg"), "commit not supported by this provider");
    REQUIRE_THROWS_WITH(provider->BranchListArgv("/root"), "branch listing not supported by this provider");
    REQUIRE_THROWS_WITH(provider->BranchSwitchArgv("/root", "dev"), "branch switching not supported by this provider");
    REQUIRE_THROWS_WITH(provider->BranchCreateArgv("/root", "dev"), "branch creation not supported by this provider");
    REQUIRE_THROWS_WITH(provider->LogArgv("x"), "log not supported by this provider");
    REQUIRE_THROWS_WITH(provider->DiffArgv("x"), "diff not supported by this provider");
    REQUIRE_THROWS_WITH(provider->WorkingDiffArgv("/root"), "full diff not supported by this provider");
}

TEST_CASE("ned/vcs-register-provider rejects a callbacks argument without :detect", "[JanetVcsProvider]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    // Missing :detect (or a non-table callbacks argument) panics inside
    // Janet, surfacing as DoString's own thrown error -- and must leave
    // nothing registered.
    REQUIRE_THROWS(env.DoString(R"(
      (ned/vcs-register-provider "detectless" {:blame-argv (fn [path] ["true"])})
    )"));
    REQUIRE_THROWS(env.DoString(R"(
      (ned/vcs-register-provider "not-a-table" 42)
    )"));
    REQUIRE(ned::editor::vcs::ActiveProviderFor("/anything") == nullptr);
}
