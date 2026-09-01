#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>

#include "Editor/TerminalTabLauncher.h"

using ned::editor::BuildKonsoleNewSessionArgv;
using ned::editor::BuildKonsoleRunCommandArgv;
using ned::editor::BuildLaunchArgv;
using ned::editor::EnvLookup;
using ned::editor::KonsoleDbusApiEnabledFromConfig;
using ned::editor::SelectTerminal;
using ned::editor::TerminalKind;

namespace {

EnvLookup FakeEnv(std::map<std::string, std::string> values) {
    return [values = std::move(values)](std::string_view name) -> std::optional<std::string> {
        const auto it = values.find(std::string(name));
        return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
}

} // namespace

TEST_CASE("SelectTerminal returns nullopt when nothing is detected", "[TerminalTabLauncher]") {
    REQUIRE_FALSE(SelectTerminal(FakeEnv({}), /*konsoleDbusApiEnabled=*/true).has_value());
}

TEST_CASE("SelectTerminal detects tmux and screen ahead of everything else", "[TerminalTabLauncher]") {
    REQUIRE(SelectTerminal(FakeEnv({{"TMUX", "/tmp/tmux-1000/default,123,0"}}), true) == TerminalKind::Tmux);
    REQUIRE(SelectTerminal(FakeEnv({{"STY", "1234.pts-0.host"}}), true) == TerminalKind::Screen);

    // tmux wins even when a GUI emulator's own var is also set (e.g. tmux
    // running inside Konsole) -- multiplexers are checked first.
    REQUIRE(SelectTerminal(FakeEnv({{"TMUX", "x"}, {"KONSOLE_VERSION", "260800"}}), true) == TerminalKind::Tmux);
}

TEST_CASE("SelectTerminal requires the Konsole D-Bus gate to be enabled", "[TerminalTabLauncher]") {
    const EnvLookup env = FakeEnv({{"KONSOLE_VERSION", "260800"}});
    REQUIRE(SelectTerminal(env, /*konsoleDbusApiEnabled=*/true) == TerminalKind::Konsole);
    // Gate off -- Konsole is skipped outright rather than attempted, since a
    // tab would open with no way to actually launch anything in it.
    REQUIRE_FALSE(SelectTerminal(env, /*konsoleDbusApiEnabled=*/false).has_value());
}

TEST_CASE("SelectTerminal detects each GUI emulator by its own env var", "[TerminalTabLauncher]") {
    REQUIRE(SelectTerminal(FakeEnv({{"GNOME_TERMINAL_SCREEN", "/org/gnome/Terminal/screen/x"}}), true) ==
            TerminalKind::GnomeTerminal);
    REQUIRE(SelectTerminal(FakeEnv({{"VTE_VERSION", "6800"}}), true) == TerminalKind::GnomeTerminal);
    REQUIRE(SelectTerminal(FakeEnv({{"WEZTERM_PANE", "1"}}), true) == TerminalKind::WezTerm);
    REQUIRE(SelectTerminal(FakeEnv({{"GHOSTTY_RESOURCES_DIR", "/usr/share/ghostty"}}), true) == TerminalKind::Ghostty);
    REQUIRE(SelectTerminal(FakeEnv({{"KITTY_WINDOW_ID", "1"}}), true) == TerminalKind::Kitty);
}

TEST_CASE("KonsoleDbusApiEnabledFromConfig reads the exact confirmed key", "[TerminalTabLauncher]") {
    REQUIRE(KonsoleDbusApiEnabledFromConfig("[KonsoleWindow]\nEnableSecuritySensitiveDBusAPI=true\n"));
    REQUIRE_FALSE(KonsoleDbusApiEnabledFromConfig("[KonsoleWindow]\nEnableSecuritySensitiveDBusAPI=false\n"));
    // Missing file/section/key all default to false, matching Konsole's own default.
    REQUIRE_FALSE(KonsoleDbusApiEnabledFromConfig(""));
    REQUIRE_FALSE(KonsoleDbusApiEnabledFromConfig("[MainWindow]\nSomeOtherKey=true\n"));
    REQUIRE_FALSE(KonsoleDbusApiEnabledFromConfig("[KonsoleWindow]\nFocusFollowsMouse=true\n"));
    // A same-named key in the wrong section doesn't count.
    REQUIRE_FALSE(
        KonsoleDbusApiEnabledFromConfig("[SomeOtherSection]\nEnableSecuritySensitiveDBusAPI=true\n[KonsoleWindow]\nX=1\n"));
    // The real key, found among other sections/keys, still works.
    REQUIRE(KonsoleDbusApiEnabledFromConfig(
        "[MainWindow]\nState=AAAA\n\n[KonsoleWindow]\nFocusFollowsMouse=true\nEnableSecuritySensitiveDBusAPI=true\n\n[Other]\nY=2\n"));
}

TEST_CASE("BuildLaunchArgv builds the exact confirmed-live command per kind", "[TerminalTabLauncher]") {
    const std::filesystem::path exe  = "/usr/local/bin/ned";
    const std::filesystem::path root = "/home/user/myproject";

    REQUIRE(BuildLaunchArgv(TerminalKind::Tmux, exe, root) ==
            std::vector<std::string>{"tmux", "new-window", "-c", root.string(), exe.string(), root.string()});
    REQUIRE(BuildLaunchArgv(TerminalKind::Screen, exe, root) ==
            std::vector<std::string>{"screen", "-X", "screen", exe.string(), root.string()});
    REQUIRE(BuildLaunchArgv(TerminalKind::GnomeTerminal, exe, root) ==
            std::vector<std::string>{"gnome-terminal", "--tab", "--working-directory=" + root.string(), "--", exe.string(),
                                     root.string()});
    REQUIRE(BuildLaunchArgv(TerminalKind::WezTerm, exe, root) ==
            std::vector<std::string>{"wezterm", "cli", "spawn", "--cwd", root.string(), "--", exe.string(), root.string()});
    REQUIRE(BuildLaunchArgv(TerminalKind::Ghostty, exe, root) ==
            std::vector<std::string>{"ghostty", "+new-window", "--working-directory=" + root.string(), "-e", exe.string(),
                                     root.string()});
    REQUIRE(BuildLaunchArgv(TerminalKind::Kitty, exe, root) ==
            std::vector<std::string>{"kitten", "@", "launch", "--type=tab", "--cwd", root.string(), exe.string(), root.string()});
    // Konsole isn't covered by this function at all -- it needs the two
    // dependent D-Bus calls below instead.
    REQUIRE(BuildLaunchArgv(TerminalKind::Konsole, exe, root).empty());
}

TEST_CASE("BuildKonsoleNewSessionArgv targets the calling window directly", "[TerminalTabLauncher]") {
    const std::vector<std::string> argv = BuildKonsoleNewSessionArgv("qdbus6", ":1.93", "/Windows/1", "/home/user/myproject");
    REQUIRE(argv == std::vector<std::string>{"qdbus6", ":1.93", "/Windows/1", "newSession", "", "/home/user/myproject"});
}

TEST_CASE("BuildKonsoleRunCommandArgv shell-quotes both paths", "[TerminalTabLauncher]") {
    const std::vector<std::string> argv =
        BuildKonsoleRunCommandArgv("qdbus6", ":1.93", "11", "/usr/local/bin/ned", "/home/user/myproject");
    REQUIRE(argv.size() == 5);
    REQUIRE(argv[0] == "qdbus6");
    REQUIRE(argv[1] == ":1.93");
    REQUIRE(argv[2] == "/Sessions/11");
    REQUIRE(argv[3] == "runCommand");
    REQUIRE(argv[4] == "'/usr/local/bin/ned' '/home/user/myproject'");
}

TEST_CASE("BuildKonsoleRunCommandArgv escapes an embedded single quote", "[TerminalTabLauncher]") {
    const std::vector<std::string> argv =
        BuildKonsoleRunCommandArgv("qdbus6", ":1.93", "11", "/usr/local/bin/ned", "/home/user/o'brien's project");
    REQUIRE(argv[4] == "'/usr/local/bin/ned' '/home/user/o'\\''brien'\\''s project'");
}
