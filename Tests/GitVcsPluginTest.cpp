#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Vcs/DiffPatch.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/PluginLoader.h"
#include "JanetTestSupport.h"

using ned::editor::process::ChildProcess;
using ned::janet::Environment;
using ned::janet::InstallEditorBindings;
using ned::janet::LoadBundledPlugins;

namespace {

struct RegistryResetGuard {
    RegistryResetGuard() {
        ned::editor::vcs::ClearRegistry();
    }
    ~RegistryResetGuard() {
        ned::editor::vcs::ClearRegistry();
    }
};

// Real, non-bundled, system-installed `git` -- tests exercising the real
// end-to-end blame path SKIP rather than fail if absent, matching
// DynamicGrammarTest.cpp/ModeOverridesTest.cpp's own convention for a
// system-dependent fixture.
bool GitAvailable() {
    try {
        ChildProcess probe({"git", "--version"});
        (void)probe.WaitForExit();
        return true;
    }
    catch (const std::runtime_error&) {
        return false;
    }
}

// Runs argv to completion, returning its combined stdout. Throws if git
// exits non-zero -- test setup should never fail silently.
std::string RunToCompletion(const std::vector<std::string>& argv) {
    ChildProcess process(argv, ned::editor::process::StderrMode::MergeWithStdout);
    std::string  output;
    for (std::string chunk = process.ReadSome(); !chunk.empty(); chunk = process.ReadSome()) {
        output += chunk;
    }
    const std::optional<int> exitCode = process.WaitForExit();
    if (!exitCode || *exitCode != 0) {
        throw std::runtime_error("test setup command failed: " + argv.front());
    }
    return output;
}

} // namespace

TEST_CASE("bundled git plugin blames a real, minimal temp git repo end to end", "[GitVcsPlugin]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }

    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    LoadBundledPlugins(env);

    const std::filesystem::path repoRoot =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-plugin-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(repoRoot);
    std::filesystem::create_directories(repoRoot);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{repoRoot};

    const std::string root = repoRoot.string();
    RunToCompletion({"git", "-C", root, "init", "-q"});
    RunToCompletion({"git", "-C", root, "config", "user.email", "ned-test@example.com"});
    RunToCompletion({"git", "-C", root, "config", "user.name", "Ned Test"});

    const std::filesystem::path filePath = repoRoot / "file.txt";
    {
        std::ofstream file(filePath);
        file << "hello world\n";
    }
    RunToCompletion({"git", "-C", root, "add", "file.txt"});
    RunToCompletion({"git", "-C", root, "commit", "-q", "-m", "initial commit"});

    auto* provider = ned::editor::vcs::ActiveProviderFor(repoRoot);
    REQUIRE(provider != nullptr);

    const auto blameSpec = provider->BlameArgv(filePath);
    REQUIRE_FALSE(blameSpec.argv.empty());

    const std::string blameOutput = RunToCompletion(blameSpec.argv);
    const auto        blameLines  = provider->ParseBlame(blameOutput);

    REQUIRE(blameLines.size() == 1);
    REQUIRE(blameLines[0].author == "Ned Test");
    REQUIRE(blameLines[0].summary == "initial commit");
    REQUIRE(blameLines[0].commitHash.size() == 40);

    const auto logSpec = provider->LogArgv(filePath);
    REQUIRE_FALSE(logSpec.argv.empty());

    const std::string logOutput  = RunToCompletion(logSpec.argv);
    const auto        logEntries = provider->ParseLog(logOutput);

    REQUIRE(logEntries.size() == 1);
    REQUIRE(logEntries[0].author == "Ned Test");
    REQUIRE(logEntries[0].summary == "initial commit");

    // Diff gutter follow-up: modify the tracked file (uncommitted) and
    // confirm the real `git diff -U0` hunk header parses correctly end to
    // end -- a single-line modification, "@@ -1 +1 @@" (no comma on either
    // side, the trickiest of the three hunk-header shapes to parse).
    {
        std::ofstream(filePath) << "hello world, changed\n";
    }

    const auto diffSpec = provider->DiffArgv(filePath);
    REQUIRE_FALSE(diffSpec.argv.empty());

    const std::string diffOutput = RunToCompletion(diffSpec.argv);
    const auto        hunks      = provider->ParseDiff(diffOutput);

    REQUIRE(hunks.size() == 1);
    REQUIRE(hunks[0].oldStart == 1);
    REQUIRE(hunks[0].oldCount == 1);
    REQUIRE(hunks[0].newStart == 1);
    REQUIRE(hunks[0].newCount == 1);
}

TEST_CASE("bundled git plugin parses porcelain status and branch lists", "[GitVcsPlugin]") {
    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    LoadBundledPlugins(env);

    // Detect only checks that a ".git" entry exists -- a bare directory is
    // enough to resolve the provider for parse-only tests, no real `git`
    // binary needed (so these never SKIP, unlike the end-to-end tests).
    const std::filesystem::path fakeRoot =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-parse-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(fakeRoot);
    std::filesystem::create_directories(fakeRoot / ".git");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{fakeRoot};

    auto* provider = ned::editor::vcs::ActiveProviderFor(fakeRoot);
    REQUIRE(provider != nullptr);

    SECTION("status: state codes, untracked, staged rename, quoted path") {
        // The trailing "x" line is malformed (below the "XY <path>" minimum
        // length) and must be dropped, not crash the parse.
        const auto entries = provider->ParseStatus(" M src/main.cpp\n"
                                                   "M  staged.cpp\n"
                                                   "?? new file.txt\n"
                                                   "R  old-name.txt -> new-name.txt\n"
                                                   "?? \"quoted path.txt\"\n"
                                                   "x\n");
        REQUIRE(entries.size() == 5);
        REQUIRE(entries[0].state == " M");
        REQUIRE(entries[0].path == "src/main.cpp");
        REQUIRE(entries[1].state == "M ");
        REQUIRE(entries[1].path == "staged.cpp");
        REQUIRE(entries[2].state == "??");
        REQUIRE(entries[2].path == "new file.txt");
        // A staged rename reports the *new* name -- the path staging or
        // visiting would want.
        REQUIRE(entries[3].state == "R ");
        REQUIRE(entries[3].path == "new-name.txt");
        // git's own double-quoting of special-character paths is stripped.
        REQUIRE(entries[4].path == "quoted path.txt");
    }

    SECTION("branch list: current marker, other-worktree marker, detached HEAD skipped") {
        const auto branches = provider->ParseBranchList("  dev\n"
                                                        "* main\n"
                                                        "+ worktree-branch\n"
                                                        "* (HEAD detached at abc1234)\n");
        REQUIRE(branches.size() == 3);
        REQUIRE(branches[0].name == "dev");
        REQUIRE_FALSE(branches[0].current);
        REQUIRE(branches[1].name == "main");
        REQUIRE(branches[1].current);
        REQUIRE(branches[2].name == "worktree-branch");
        REQUIRE_FALSE(branches[2].current);
    }
}

TEST_CASE("bundled git plugin runs status/stage/unstage/commit/branch against a real temp repo end to end",
          "[GitVcsPlugin]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }

    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    LoadBundledPlugins(env);

    const std::filesystem::path repoRoot =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-vocab-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(repoRoot);
    std::filesystem::create_directories(repoRoot);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{repoRoot};

    const std::string root = repoRoot.string();
    RunToCompletion({"git", "-C", root, "init", "-q", "-b", "main"});
    RunToCompletion({"git", "-C", root, "config", "user.email", "ned-test@example.com"});
    RunToCompletion({"git", "-C", root, "config", "user.name", "Ned Test"});

    const std::filesystem::path filePath = repoRoot / "file.txt";
    {
        std::ofstream(filePath) << "hello world\n";
    }
    RunToCompletion({"git", "-C", root, "add", "file.txt"});
    RunToCompletion({"git", "-C", root, "commit", "-q", "-m", "initial commit"});

    auto* provider = ned::editor::vcs::ActiveProviderFor(repoRoot);
    REQUIRE(provider != nullptr);

    // Modify tracked + add untracked, then walk the whole flow.
    {
        std::ofstream(filePath) << "hello world, changed\n";
    }
    {
        std::ofstream(repoRoot / "new.txt") << "brand new\n";
    }

    auto statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.size() == 2);
    REQUIRE(statusEntries[0].state == " M");
    REQUIRE(statusEntries[0].path == "file.txt");
    REQUIRE(statusEntries[1].state == "??");
    REQUIRE(statusEntries[1].path == "new.txt");

    // Stage the modification; its state's index column flips.
    RunToCompletion(provider->StageArgv(filePath).argv);
    statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries[0].state == "M ");

    // Unstage it back.
    RunToCompletion(provider->UnstageArgv(filePath).argv);
    statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries[0].state == " M");

    // Stage + commit; the working tree's modification disappears from
    // status and the commit lands in the log.
    RunToCompletion(provider->StageArgv(filePath).argv);
    RunToCompletion(provider->CommitArgv(repoRoot, "second commit").argv);
    statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.size() == 1); // only the untracked file remains
    REQUIRE(statusEntries[0].state == "??");

    const auto logEntries = provider->ParseLog(RunToCompletion(provider->LogArgv(filePath).argv));
    REQUIRE(logEntries.size() == 2);
    REQUIRE(logEntries[0].summary == "second commit");

    // Branches: create one, confirm it's current, switch back.
    auto branches = provider->ParseBranchList(RunToCompletion(provider->BranchListArgv(repoRoot).argv));
    REQUIRE(branches.size() == 1);
    REQUIRE(branches[0].name == "main");
    REQUIRE(branches[0].current);

    RunToCompletion(provider->BranchCreateArgv(repoRoot, "feature").argv);
    branches = provider->ParseBranchList(RunToCompletion(provider->BranchListArgv(repoRoot).argv));
    REQUIRE(branches.size() == 2);
    for (const auto& branch : branches) {
        REQUIRE(branch.current == (branch.name == "feature"));
    }

    RunToCompletion(provider->BranchSwitchArgv(repoRoot, "main").argv);
    branches = provider->ParseBranchList(RunToCompletion(provider->BranchListArgv(repoRoot).argv));
    for (const auto& branch : branches) {
        REQUIRE(branch.current == (branch.name == "main"));
    }
}

TEST_CASE("bundled git plugin stages and unstages a single hunk end to end", "[GitVcsPlugin]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }

    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    LoadBundledPlugins(env);

    const std::filesystem::path repoRoot =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-hunk-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(repoRoot);
    std::filesystem::create_directories(repoRoot);
    // The patch file lives OUTSIDE the repo so it can't pollute the status
    // assertions below as an untracked entry.
    const std::filesystem::path patchPath =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-hunk-test-patch-" + std::to_string(::getpid()) + ".diff");
    struct Cleanup {
        std::filesystem::path repo;
        std::filesystem::path patch;
        ~Cleanup() {
            std::filesystem::remove_all(repo);
            std::filesystem::remove(patch);
        }
    } cleanup{repoRoot, patchPath};

    const std::string root = repoRoot.string();
    RunToCompletion({"git", "-C", root, "init", "-q", "-b", "main"});
    RunToCompletion({"git", "-C", root, "config", "user.email", "ned-test@example.com"});
    RunToCompletion({"git", "-C", root, "config", "user.name", "Ned Test"});

    const std::filesystem::path filePath = repoRoot / "file.txt";
    {
        std::ofstream(filePath) << "line one\nline two\nline three\nline four\nline five\nline six\nline seven\n";
    }
    RunToCompletion({"git", "-C", root, "add", "file.txt"});
    RunToCompletion({"git", "-C", root, "commit", "-q", "-m", "initial commit"});

    // Two well-separated single-line edits -> two distinct -U0 hunks.
    {
        std::ofstream(filePath) << "line one\nline TWO\nline three\nline four\nline five\nline six\nline SEVEN\n";
    }

    auto* provider = ned::editor::vcs::ActiveProviderFor(repoRoot);
    REQUIRE(provider != nullptr);

    // Stage only the line-2 hunk, exactly the chain VcsRunner::RequestHunkApply runs.
    const std::string rawDiff = RunToCompletion(provider->DiffArgv(filePath).argv);
    const auto        patch   = ned::editor::vcs::ExtractHunkPatch(rawDiff, 2);
    REQUIRE(patch.has_value());
    REQUIRE(patch->find("+line TWO") != std::string::npos);
    REQUIRE(patch->find("SEVEN") == std::string::npos);
    {
        std::ofstream(patchPath) << *patch;
    }
    RunToCompletion(provider->StagePatchArgv(repoRoot, patchPath).argv);

    // Partially staged: index and worktree both differ -> "MM".
    auto statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.size() == 1);
    REQUIRE(statusEntries[0].state == "MM");
    REQUIRE(statusEntries[0].path == "file.txt");

    // The cached diff holds only the staged hunk...
    const std::string cachedDiff = RunToCompletion(provider->StagedDiffArgv(filePath).argv);
    REQUIRE(cachedDiff.find("+line TWO") != std::string::npos);
    REQUIRE(cachedDiff.find("SEVEN") == std::string::npos);
    // ...and the worktree diff only the unstaged one.
    const std::string remainingDiff = RunToCompletion(provider->DiffArgv(filePath).argv);
    REQUIRE(remainingDiff.find("+line SEVEN") != std::string::npos);
    REQUIRE(remainingDiff.find("TWO") == std::string::npos);

    // Unstage it back: the hunk comes from the CACHED diff (it's no longer
    // in the worktree one), applied in reverse.
    const auto unstagePatch = ned::editor::vcs::ExtractHunkPatch(cachedDiff, 2);
    REQUIRE(unstagePatch.has_value());
    {
        std::ofstream(patchPath) << *unstagePatch;
    }
    RunToCompletion(provider->UnstagePatchArgv(repoRoot, patchPath).argv);

    statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.size() == 1);
    REQUIRE(statusEntries[0].state == " M"); // nothing staged anymore, both edits back in the worktree only
}
