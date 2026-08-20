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
    RegistryResetGuard() { ned::editor::vcs::ClearRegistry(); }
    ~RegistryResetGuard() { ned::editor::vcs::ClearRegistry(); }
};

// Real, non-bundled, system-installed `git` -- tests exercising the real
// end-to-end blame path SKIP rather than fail if absent, matching
// DynamicGrammarTest.cpp/ModeOverridesTest.cpp's own convention for a
// system-dependent fixture.
bool GitAvailable() {
    try {
        ChildProcess probe({"git", "--version"});
        (void) probe.WaitForExit();
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
        ~Cleanup() { std::filesystem::remove_all(path); }
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

    const std::string logOutput = RunToCompletion(logSpec.argv);
    const auto        logEntries = provider->ParseLog(logOutput);

    REQUIRE(logEntries.size() == 1);
    REQUIRE(logEntries[0].author == "Ned Test");
    REQUIRE(logEntries[0].summary == "initial commit");

    // Diff gutter follow-up: modify the tracked file (uncommitted) and
    // confirm the real `git diff -U0` hunk header parses correctly end to
    // end -- a single-line modification, "@@ -1 +1 @@" (no comma on either
    // side, the trickiest of the three hunk-header shapes to parse).
    { std::ofstream(filePath) << "hello world, changed\n"; }

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
