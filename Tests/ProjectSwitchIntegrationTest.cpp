// Split out of ProjectSwitchTest.cpp (terminal-integration follow-up):
// ActivateProjectRoot has no test seam over TerminalTabLauncher's real
// DetectTerminal()/TryOpenInNewTab -- calling it against a directory that
// actually exists genuinely attempts the new-tab tier. On a machine with no
// detectable terminal/multiplexer that's a no-op past a failed spawn, but on
// a real interactive session (this codebase's own dev machine runs inside
// Konsole with the D-Bus gate enabled) it truly opens a new Konsole tab
// pointed at "/tmp" every time the suite runs. Gated behind
// NED_TEST_TERMINAL_INTEGRATION (CMakeLists.txt) for the same reason as
// PtyProcessTest.cpp -- opt in when you actually want to exercise it.

#include <catch2/catch_test_macros.hpp>

#include "Editor/PendingReExec.h"
#include "Editor/ProjectSwitch.h"

using ned::editor::ActivateProjectRoot;
using ned::editor::ProjectActivationOutcome;
using ned::editor::SetProjectOpenCommand;

namespace {

struct RestoreProjectOpenCommand {
    ~RestoreProjectOpenCommand() {
        ned::editor::ResetProjectOpenCommandForTesting();
        ned::editor::ResetPendingReExecForTesting();
    }
};

} // namespace

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

TEST_CASE("A failing custom command falls through to replace-in-place instead of reporting failure", "[ProjectSwitch]") {
    const RestoreProjectOpenCommand restore;

    SetProjectOpenCommand({"/bin/false"});
    REQUIRE(ActivateProjectRoot("/tmp") == ProjectActivationOutcome::ReplacingInPlace);
    REQUIRE(ned::editor::TakePendingReExec().has_value());
}
