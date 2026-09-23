#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Vcs/DiffPatch.h"
#include "Editor/Vcs/ProviderRegistry.h"
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
// ModeOverridesTest.cpp's own convention for a
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

    // Full commit diff view follow-up: `git show`'s own real, non-U0 diff
    // for "initial commit" -- a pure new-file addition, run through
    // ParseDiffHunks end to end exactly like WorkingDiffArgv's own case
    // below, just scoped to one commit instead of the whole working tree.
    const auto commitDiffSpec = provider->CommitDiffArgv(repoRoot, logEntries[0].commitHash);
    REQUIRE_FALSE(commitDiffSpec.argv.empty());

    const std::string commitDiffOutput = RunToCompletion(commitDiffSpec.argv);
    const auto        commitHunks      = ned::editor::vcs::ParseDiffHunks(commitDiffOutput);

    REQUIRE(commitHunks.size() == 1);
    REQUIRE(commitHunks[0].filePath == "file.txt");
    REQUIRE(commitHunks[0].oldCount == 0); // pure addition -- nothing on the old side
    REQUIRE(commitHunks[0].newStart == 1);
    REQUIRE(commitHunks[0].newCount == 1);
    REQUIRE(commitHunks[0].bodyText.find("+hello world") != std::string::npos);

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

    // Multibuffers follow-up: the root-scoped, real-context working-tree
    // diff, run through ParseDiffHunks end to end against real git output.
    const auto workingDiffSpec = provider->WorkingDiffArgv(repoRoot);
    REQUIRE_FALSE(workingDiffSpec.argv.empty());

    const std::string workingDiffOutput = RunToCompletion(workingDiffSpec.argv);
    const auto        workingHunks      = ned::editor::vcs::ParseDiffHunks(workingDiffOutput);

    REQUIRE(workingHunks.size() == 1);
    REQUIRE(workingHunks[0].filePath == "file.txt");
    REQUIRE(workingHunks[0].newStart == 1);
    REQUIRE(workingHunks[0].newCount == 1);
    REQUIRE(workingHunks[0].bodyText.find("+hello world, changed") != std::string::npos);
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

    // VcsPanel amend follow-up: previous-commit-message reads HEAD's own
    // message verbatim, and amend-commit-argv replaces it in place --
    // still one commit afterward, not two, and the log's own summary
    // picks up the amended text.
    const std::string previousMessage = RunToCompletion(provider->PreviousCommitMessageArgv(repoRoot).argv);
    REQUIRE(previousMessage.find("second commit") != std::string::npos);

    RunToCompletion(provider->AmendCommitArgv(repoRoot, "second commit, amended").argv);
    const auto logAfterAmend = provider->ParseLog(RunToCompletion(provider->LogArgv(filePath).argv));
    REQUIRE(logAfterAmend.size() == 2); // still two commits total, not three
    REQUIRE(logAfterAmend[0].summary == "second commit, amended");

    // VcsPanel commit-variants follow-up: extend-commit-argv folds a fresh
    // staged change into HEAD, keeping the (now-amended) message verbatim
    // -- still two commits total, and the log's summary is untouched.
    {
        std::ofstream(filePath, std::ios::app) << "more content\n";
    }
    RunToCompletion(provider->StageArgv(filePath).argv);
    RunToCompletion(provider->ExtendCommitArgv(repoRoot).argv);
    const auto logAfterExtend = provider->ParseLog(RunToCompletion(provider->LogArgv(filePath).argv));
    REQUIRE(logAfterExtend.size() == 2); // still two commits total
    REQUIRE(logAfterExtend[0].summary == "second commit, amended"); // message untouched
    const auto statusAfterExtend = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusAfterExtend.size() == 1); // only the still-untracked new.txt remains
    REQUIRE(statusAfterExtend[0].state == "??");

    // Reword follow-up: reword-commit-argv replaces HEAD's message without
    // touching its tree -- unlike amend/extend above, it must leave a
    // concurrently staged change alone entirely (still staged afterward,
    // not folded into the commit and not lost).
    {
        std::ofstream(filePath, std::ios::app) << "a staged-but-not-committed line\n";
    }
    RunToCompletion(provider->StageArgv(filePath).argv);
    RunToCompletion(provider->RewordCommitArgv(repoRoot, "second commit, reworded").argv);
    const auto logAfterReword = provider->ParseLog(RunToCompletion(provider->LogArgv(filePath).argv));
    REQUIRE(logAfterReword.size() == 2); // still two commits total, not three
    REQUIRE(logAfterReword[0].summary == "second commit, reworded");
    const auto statusAfterReword = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusAfterReword.size() == 2); // the staged change survived untouched
    REQUIRE(statusAfterReword[0].state == "M "); // still staged, not committed
    REQUIRE(statusAfterReword[0].path == "file.txt");
    REQUIRE(statusAfterReword[1].state == "??");
    REQUIRE(statusAfterReword[1].path == "new.txt");
    RunToCompletion(provider->UnstageArgv(filePath).argv); // leave a clean slate for the branch assertions below
    RunToCompletion({"git", "-C", root, "checkout", "--", "file.txt"});

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

    // Stage only the line-2 hunk, exactly the chain Runner::RequestHunkApply runs.
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

TEST_CASE("bundled git plugin reverts a single hunk from the working tree end to end", "[GitVcsPlugin]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }

    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    LoadBundledPlugins(env);

    const std::filesystem::path repoRoot =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-revert-hunk-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(repoRoot);
    std::filesystem::create_directories(repoRoot);
    const std::filesystem::path patchPath =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-revert-hunk-test-patch-" + std::to_string(::getpid()) + ".diff");
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

    // Two well-separated single-line edits -> two distinct -U0 hunks, same
    // shape the stage/unstage test above uses.
    {
        std::ofstream(filePath) << "line one\nline TWO\nline three\nline four\nline five\nline six\nline SEVEN\n";
    }

    auto* provider = ned::editor::vcs::ActiveProviderFor(repoRoot);
    REQUIRE(provider != nullptr);

    // Revert only the line-2 hunk -- the exact chain Runner::RequestHunkRevert runs.
    const std::string rawDiff = RunToCompletion(provider->DiffArgv(filePath).argv);
    const auto        patch   = ned::editor::vcs::ExtractHunkPatch(rawDiff, 2);
    REQUIRE(patch.has_value());
    REQUIRE(patch->find("+line TWO") != std::string::npos);
    REQUIRE(patch->find("SEVEN") == std::string::npos);
    {
        std::ofstream(patchPath) << *patch;
    }
    RunToCompletion(provider->RevertPatchArgv(repoRoot, patchPath).argv);

    // Line 2 is back to its committed content; line 7's still-unstaged edit
    // (a separate hunk) is untouched.
    std::ifstream     reverted(filePath);
    std::stringstream contents;
    contents << reverted.rdbuf();
    REQUIRE(contents.str() == "line one\nline two\nline three\nline four\nline five\nline six\nline SEVEN\n");

    auto statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.size() == 1);
    REQUIRE(statusEntries[0].state == " M"); // still one unstaged edit -- line 7's, not reverted
}

TEST_CASE("bundled git plugin runs revert/stash against a real temp repo end to end", "[GitVcsPlugin]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }

    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    LoadBundledPlugins(env);

    const std::filesystem::path repoRoot =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-revert-stash-test-" + std::to_string(::getpid()));
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

    // Revert: a working-tree edit (both staged and further edited)
    // disappears entirely, back to HEAD's own content -- distinct from
    // unstage, which only moves the index.
    {
        std::ofstream(filePath) << "staged change\n";
    }
    RunToCompletion(provider->StageArgv(filePath).argv);
    {
        std::ofstream(filePath, std::ios::app) << "further unstaged edit\n";
    }
    auto statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.size() == 1);
    REQUIRE(statusEntries[0].state == "MM"); // tracked file: staged mod + further unstaged mod
    RunToCompletion(provider->RevertArgv(filePath).argv);
    statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.empty());
    {
        std::ifstream in(filePath);
        std::string   content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(content == "hello world\n");
    }

    // Stash: list/push/pop/drop.
    auto stashes = provider->ParseStashList(RunToCompletion(provider->StashListArgv(repoRoot).argv));
    REQUIRE(stashes.empty());

    {
        std::ofstream(filePath) << "hello world, stashable\n";
    }
    RunToCompletion(provider->StashPushArgv(repoRoot, "my test stash").argv);
    statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.empty()); // stashed away, working tree clean again
    stashes = provider->ParseStashList(RunToCompletion(provider->StashListArgv(repoRoot).argv));
    REQUIRE(stashes.size() == 1);
    REQUIRE(stashes[0].ref == "stash@{0}");
    REQUIRE(stashes[0].message.find("my test stash") != std::string::npos);

    RunToCompletion(provider->StashPopArgv(repoRoot, stashes[0].ref).argv);
    statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.size() == 1); // the edit is back
    stashes = provider->ParseStashList(RunToCompletion(provider->StashListArgv(repoRoot).argv));
    REQUIRE(stashes.empty()); // pop removes the stash entry, unlike apply

    RunToCompletion(provider->StashPushArgv(repoRoot, "").argv); // empty message -> git's own default
    stashes = provider->ParseStashList(RunToCompletion(provider->StashListArgv(repoRoot).argv));
    REQUIRE(stashes.size() == 1);
    RunToCompletion(provider->StashDropArgv(repoRoot, stashes[0].ref).argv);
    stashes = provider->ParseStashList(RunToCompletion(provider->StashListArgv(repoRoot).argv));
    REQUIRE(stashes.empty());
    statusEntries = provider->ParseStatus(RunToCompletion(provider->StatusArgv(repoRoot).argv));
    REQUIRE(statusEntries.empty()); // drop discards the change entirely, doesn't restore it
}

TEST_CASE("bundled git plugin runs push/pull/fetch/ahead-behind against a real bare remote end to end",
          "[GitVcsPlugin]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }

    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    LoadBundledPlugins(env);

    const std::filesystem::path base =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-push-pull-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{base};

    const std::filesystem::path bareRemote = base / "remote.git";
    const std::filesystem::path repoA      = base / "a"; // pushes/fetches against bareRemote
    const std::filesystem::path repoB      = base / "b"; // makes the upstream commit repoA fetches/pulls

    RunToCompletion({"git", "init", "-q", "--bare", "-b", "main", bareRemote.string()});

    RunToCompletion({"git", "clone", "-q", bareRemote.string(), repoA.string()});
    RunToCompletion({"git", "-C", repoA.string(), "config", "user.email", "ned-test@example.com"});
    RunToCompletion({"git", "-C", repoA.string(), "config", "user.name", "Ned Test"});
    {
        std::ofstream(repoA / "file.txt") << "hello from a\n";
    }
    RunToCompletion({"git", "-C", repoA.string(), "add", "file.txt"});
    RunToCompletion({"git", "-C", repoA.string(), "commit", "-q", "-m", "initial commit"});
    RunToCompletion({"git", "-C", repoA.string(), "push", "-q", "-u", "origin", "main"});

    auto* provider = ned::editor::vcs::ActiveProviderFor(repoA);
    REQUIRE(provider != nullptr);

    // Up to date immediately after the push.
    auto ab = provider->ParseAheadBehind(RunToCompletion(provider->AheadBehindArgv(repoA).argv));
    REQUIRE(ab.ahead == 0);
    REQUIRE(ab.behind == 0);

    // A local-only commit puts repoA one ahead of its upstream.
    {
        std::ofstream(repoA / "file.txt", std::ios::app) << "a local commit\n";
    }
    RunToCompletion({"git", "-C", repoA.string(), "commit", "-aq", "-m", "local commit"});
    ab = provider->ParseAheadBehind(RunToCompletion(provider->AheadBehindArgv(repoA).argv));
    REQUIRE(ab.ahead == 1);
    REQUIRE(ab.behind == 0);

    // PushArgv (bare, no --set-upstream -- relies on the tracking branch
    // already configured by the -u push above) brings it back even.
    RunToCompletion(provider->PushArgv(repoA).argv);
    ab = provider->ParseAheadBehind(RunToCompletion(provider->AheadBehindArgv(repoA).argv));
    REQUIRE(ab.ahead == 0);
    REQUIRE(ab.behind == 0);

    // A second clone pushes a commit repoA doesn't have yet.
    RunToCompletion({"git", "clone", "-q", bareRemote.string(), repoB.string()});
    RunToCompletion({"git", "-C", repoB.string(), "config", "user.email", "ned-test@example.com"});
    RunToCompletion({"git", "-C", repoB.string(), "config", "user.name", "Ned Test"});
    {
        std::ofstream(repoB / "file.txt", std::ios::app) << "a remote commit\n";
    }
    RunToCompletion({"git", "-C", repoB.string(), "commit", "-aq", "-m", "remote commit"});
    RunToCompletion({"git", "-C", repoB.string(), "push", "-q"});

    // FetchArgv alone updates the remote-tracking ref, not the local branch.
    RunToCompletion(provider->FetchArgv(repoA).argv);
    ab = provider->ParseAheadBehind(RunToCompletion(provider->AheadBehindArgv(repoA).argv));
    REQUIRE(ab.ahead == 0);
    REQUIRE(ab.behind == 1);
    {
        std::ifstream in(repoA / "file.txt");
        std::string   content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(content.find("a remote commit") == std::string::npos); // not merged into the working tree yet
    }

    // PullArgv (fetch + merge) brings the local branch up to date.
    RunToCompletion(provider->PullArgv(repoA).argv);
    ab = provider->ParseAheadBehind(RunToCompletion(provider->AheadBehindArgv(repoA).argv));
    REQUIRE(ab.ahead == 0);
    REQUIRE(ab.behind == 0);
    {
        std::ifstream in(repoA / "file.txt");
        std::string   content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(content.find("a remote commit") != std::string::npos);
    }
}

namespace {

// Like RunToCompletion, but for a command expected to stop on a conflict
// (non-zero exit) -- returns the exit code instead of throwing.
int RunAllowingFailure(const std::vector<std::string>& argv) {
    ChildProcess process(argv, ned::editor::process::StderrMode::MergeWithStdout);
    for (std::string chunk = process.ReadSome(); !chunk.empty(); chunk = process.ReadSome()) {
    }
    const std::optional<int> exitCode = process.WaitForExit();
    return exitCode.value_or(-1);
}

ned::editor::vcs::SequenceState ProbeSequence(ned::editor::vcs::Provider& provider, const std::filesystem::path& root) {
    return provider.ParseSequenceState(RunToCompletion(provider.SequenceStateArgv(root).argv));
}

} // namespace

TEST_CASE("bundled git plugin probes and drives an in-progress rebase, cherry-pick and merge", "[GitVcsPlugin]") {
    if (!GitAvailable()) {
        SKIP("git not found on $PATH");
    }

    RegistryResetGuard guard;
    Environment&       env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);
    LoadBundledPlugins(env);

    const std::filesystem::path repo =
        std::filesystem::temp_directory_path() / ("ned-git-vcs-sequence-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(repo);
    std::filesystem::create_directories(repo);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{repo};

    const std::string root = repo.string();
    RunToCompletion({"git", "init", "-q", "-b", "main", root});
    RunToCompletion({"git", "-C", root, "config", "user.email", "ned-test@example.com"});
    RunToCompletion({"git", "-C", root, "config", "user.name", "Ned Test"});
    const auto commitFile = [&](const std::string& content, const std::string& message) {
        std::ofstream(repo / "file.txt") << content;
        RunToCompletion({"git", "-C", root, "add", "file.txt"});
        RunToCompletion({"git", "-C", root, "commit", "-q", "-m", message});
    };
    commitFile("base\n", "base");
    RunToCompletion({"git", "-C", root, "checkout", "-q", "-b", "topic"});
    commitFile("topic one\n", "topic one");
    commitFile("topic two\n", "topic two");
    RunToCompletion({"git", "-C", root, "checkout", "-q", "main"});
    commitFile("main\n", "main");

    auto* provider = ned::editor::vcs::ActiveProviderFor(repo);
    REQUIRE(provider != nullptr);

    SECTION("nothing in progress reports an empty kind") {
        REQUIRE(ProbeSequence(*provider, repo).kind.empty());
    }

    SECTION("a conflicted rebase reports its progress, continues, skips and finishes") {
        RunToCompletion({"git", "-C", root, "checkout", "-q", "topic"});
        REQUIRE(RunAllowingFailure({"git", "-C", root, "rebase", "main"}) != 0);

        auto state = ProbeSequence(*provider, repo);
        REQUIRE(state.kind == "rebase");
        REQUIRE(state.step == 1);
        REQUIRE(state.total == 2);

        // Resolving the first pick and continuing stops again on the second,
        // which also touches the same line.
        std::ofstream(repo / "file.txt") << "resolved\n";
        RunToCompletion({"git", "-C", root, "add", "file.txt"});
        REQUIRE(RunAllowingFailure(provider->SequenceContinueArgv(repo, state.kind).argv) != 0);
        state = ProbeSequence(*provider, repo);
        REQUIRE(state.kind == "rebase");
        REQUIRE(state.step == 2);

        RunToCompletion(provider->SequenceSkipArgv(repo, state.kind).argv);
        REQUIRE(ProbeSequence(*provider, repo).kind.empty());
        REQUIRE(RunToCompletion({"git", "-C", root, "log", "-1", "--pretty=%s"}) == "topic one\n");
    }

    SECTION("abort restores the pre-rebase state") {
        RunToCompletion({"git", "-C", root, "checkout", "-q", "topic"});
        REQUIRE(RunAllowingFailure({"git", "-C", root, "rebase", "main"}) != 0);
        RunToCompletion(provider->SequenceAbortArgv(repo, "rebase").argv);
        REQUIRE(ProbeSequence(*provider, repo).kind.empty());
        REQUIRE(RunToCompletion({"git", "-C", root, "log", "-1", "--pretty=%s"}) == "topic two\n");
    }

    SECTION("a conflicted cherry-pick continues without an editor") {
        REQUIRE(RunAllowingFailure({"git", "-C", root, "cherry-pick", "topic~1"}) != 0);
        const auto state = ProbeSequence(*provider, repo);
        REQUIRE(state.kind == "cherry-pick");
        REQUIRE(state.total == 0);

        std::ofstream(repo / "file.txt") << "resolved\n";
        RunToCompletion({"git", "-C", root, "add", "file.txt"});
        RunToCompletion(provider->SequenceContinueArgv(repo, state.kind).argv);
        REQUIRE(ProbeSequence(*provider, repo).kind.empty());
        REQUIRE(RunToCompletion({"git", "-C", root, "log", "-1", "--pretty=%s"}) == "topic one\n");
    }

    SECTION("a conflicted merge continues with git's own message and has no skip") {
        REQUIRE(RunAllowingFailure({"git", "-C", root, "merge", "-q", "topic"}) != 0);
        const auto state = ProbeSequence(*provider, repo);
        REQUIRE(state.kind == "merge");
        REQUIRE_THROWS(provider->SequenceSkipArgv(repo, state.kind));

        std::ofstream(repo / "file.txt") << "resolved\n";
        RunToCompletion({"git", "-C", root, "add", "file.txt"});
        RunToCompletion(provider->SequenceContinueArgv(repo, state.kind).argv);
        REQUIRE(ProbeSequence(*provider, repo).kind.empty());
        REQUIRE(RunToCompletion({"git", "-C", root, "log", "-1", "--pretty=%s"}).starts_with("Merge branch 'topic'"));
    }
}
