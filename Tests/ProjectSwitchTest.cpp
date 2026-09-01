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

TEST_CASE("ActivateProjectRoot reports RootMissing for a nonexistent directory without touching anything else",
          "[ProjectSwitch]") {
    const RestoreProjectOpenCommand restore;

    // A custom command IS configured here specifically to prove it's never
    // reached -- RootMissing is checked and returned before any tier runs.
    // Safe to run unconditionally: RootMissing is returned before
    // ActivateProjectRoot ever calls TryOpenInNewTab, unlike the
    // real-root cases in Tests/ProjectSwitchIntegrationTest.cpp.
    SetProjectOpenCommand({"/bin/true"});
    REQUIRE(ActivateProjectRoot("/this/path/definitely/does/not/exist/anywhere") ==
            ProjectActivationOutcome::RootMissing);
    REQUIRE_FALSE(ned::editor::TakePendingReExec().has_value());
}
