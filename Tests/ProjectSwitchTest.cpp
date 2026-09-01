#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "Editor/PendingReExec.h"
#include "Editor/ProjectSwitch.h"

using ned::editor::ActivateProjectRoot;
using ned::editor::ProjectActivationOutcome;
using ned::editor::ProjectOpenCommand;
using ned::editor::SetProjectOpenCommand;
using ned::editor::SubstituteProjectOpenCommandArgv;

namespace {

struct RestoreProjectOpenCommand {
    ~RestoreProjectOpenCommand() {
        ned::editor::ResetProjectOpenCommandForTesting();
        ned::editor::ResetPendingReExecForTesting();
    }
};

} // namespace

TEST_CASE("SubstituteProjectOpenCommandArgv replaces every {root} occurrence", "[ProjectSwitch]") {
    const std::filesystem::path root = "/home/user/myproject";

    const std::vector<std::string> result =
        SubstituteProjectOpenCommandArgv({"tmux", "new-window", "-c", "{root}", "ned", "{root}"}, root);
    REQUIRE(result == std::vector<std::string>{"tmux", "new-window", "-c", root.string(), "ned", root.string()});
}

TEST_CASE("SubstituteProjectOpenCommandArgv handles multiple placeholders in one element", "[ProjectSwitch]") {
    // Literal "/" between the two placeholders in the template survives
    // untouched -- only "{root}" itself is ever replaced.
    const std::vector<std::string> result = SubstituteProjectOpenCommandArgv({"{root}/{root}"}, "/a/b");
    REQUIRE(result == std::vector<std::string>{"/a/b//a/b"});
}

TEST_CASE("SubstituteProjectOpenCommandArgv leaves elements with no placeholder unchanged", "[ProjectSwitch]") {
    const std::vector<std::string> result = SubstituteProjectOpenCommandArgv({"--flag", "plain-value"}, "/a/b");
    REQUIRE(result == std::vector<std::string>{"--flag", "plain-value"});
}

TEST_CASE("SubstituteProjectOpenCommandArgv on an empty template is empty", "[ProjectSwitch]") {
    REQUIRE(SubstituteProjectOpenCommandArgv({}, "/a/b").empty());
}

TEST_CASE("SetProjectOpenCommand/ProjectOpenCommand round-trip, empty clears", "[ProjectSwitch]") {
    const RestoreProjectOpenCommand restore;

    REQUIRE_FALSE(ProjectOpenCommand().has_value());

    SetProjectOpenCommand({"my-tool", "--open", "{root}"});
    REQUIRE(ProjectOpenCommand() == std::vector<std::string>{"my-tool", "--open", "{root}"});

    SetProjectOpenCommand({});
    REQUIRE_FALSE(ProjectOpenCommand().has_value());
}

TEST_CASE("ActivateProjectRoot runs a configured custom command when no terminal is detected", "[ProjectSwitch]") {
    const RestoreProjectOpenCommand restore;

    // /bin/true always exits 0 and touches nothing -- exercising the real
    // custom-command spawn path without any real terminal side effect. This
    // machine's test environment has no detectable terminal/multiplexer set
    // (TerminalTabLauncherTest's own live scratch check confirmed
    // DetectTerminal() returns nullopt here), so this reaches the
    // custom-command tier rather than the new-tab one.
    SetProjectOpenCommand({"/bin/true"});
    REQUIRE(ActivateProjectRoot("/tmp") == ProjectActivationOutcome::RanCustomCommand);
}

TEST_CASE("ActivateProjectRoot falls through to replace-in-place when nothing else works", "[ProjectSwitch]") {
    const RestoreProjectOpenCommand restore;

    // No custom command configured, and this test binary's own environment
    // has no detectable terminal -- the only remaining tier is
    // replace-in-place, which just sets PendingReExec rather than actually
    // execv()'ing (that only ever happens in main.cpp, never here).
    REQUIRE(ActivateProjectRoot("/tmp") == ProjectActivationOutcome::ReplacingInPlace);

    const auto pending = ned::editor::TakePendingReExec();
    REQUIRE(pending.has_value());
    REQUIRE(pending->root == "/tmp");
    // The resolved executable is *this test binary* (/proc/self/exe from
    // inside ned_tests) -- not ned itself, but that's exactly the real
    // mechanism under test, just observed from a different binary.
    REQUIRE_FALSE(pending->executablePath.empty());
}

TEST_CASE("ActivateProjectRoot reports RootMissing for a nonexistent directory without touching anything else",
          "[ProjectSwitch]") {
    const RestoreProjectOpenCommand restore;

    // A custom command IS configured here specifically to prove it's never
    // reached -- RootMissing is checked and returned before any tier runs.
    SetProjectOpenCommand({"/bin/true"});
    REQUIRE(ActivateProjectRoot("/this/path/definitely/does/not/exist/anywhere") ==
            ProjectActivationOutcome::RootMissing);
    REQUIRE_FALSE(ned::editor::TakePendingReExec().has_value());
}

TEST_CASE("A failing custom command falls through to replace-in-place instead of reporting failure", "[ProjectSwitch]") {
    const RestoreProjectOpenCommand restore;

    SetProjectOpenCommand({"/bin/false"});
    REQUIRE(ActivateProjectRoot("/tmp") == ProjectActivationOutcome::ReplacingInPlace);
    REQUIRE(ned::editor::TakePendingReExec().has_value());
}
