//
// Named-projects follow-up: opening another project as a sibling process in
// a new tab/window of whatever terminal/multiplexer this ned process is
// already running inside, leaving the current process completely untouched
// -- the preferred first rung of ProjectSwitch.h's activate-root chain
// (detected terminal -> configured custom command -> replace-in-place).
//
// Every mechanism here was verified against a real, live instance of its
// terminal during implementation, not assumed from documentation alone --
// see this file's own .cpp for the specific quirks each one turned up
// (WezTerm has no `--new-tab` flag at all; Konsole gates command injection
// behind a real, off-by-default security setting -- konsolerc's
// `[KonsoleWindow]`/`EnableSecuritySensitiveDBusAPI`, never written by ned
// itself, purely the user's own call; Ghostty's `+new-window` opens a
// window, not a tab, and silently ignores a bad flag rather than erroring;
// Kitty's own remote-control gate, `allow_remote_control`/`listen_on` in
// kitty.conf, fails cleanly with no window created at all when off, unlike
// Konsole's two-step split, so it needs no special-casing). Deliberately no
// X11-specific fallback (`xdotool`/`wmctrl`-style generic window control) --
// every mechanism below is already display-server-agnostic, mirroring
// Clipboard.h's own Wayland-only precedent for primary-selection paste.
// Alacritty and plain xterm were checked too and confirmed to have no
// CLI/IPC way to join an already-running instance at all -- not included,
// and not a gap: launching a fully disconnected process adds nothing over
// ProjectSwitch.h's own custom-command/replace-in-place fallback tiers.
//
// SelectTerminal/KonsoleDbusApiEnabledFromConfig/BuildLaunchArgv/
// BuildKonsoleNewSessionArgv/BuildKonsoleRunCommandArgv are pure and
// unit-tested; DetectTerminal/TryOpenInNewTab do real environment/file/
// process work and are verified live instead, the same split Clipboard.h's
// own DetectPlatformTools (untested, real getenv) vs. BuildOsc52CopySequence
// (pure, tested) draws.
//

#ifndef NED_EDITOR_TERMINALTABLAUNCHER_H
#define NED_EDITOR_TERMINALTABLAUNCHER_H

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

enum class TerminalKind { Tmux,
                          Screen,
                          Konsole,
                          GnomeTerminal,
                          WezTerm,
                          Ghostty,
                          Kitty };

// Injectable environment-variable lookup -- nullopt for unset/empty, same
// convention std::getenv-wrapping code throughout this codebase uses.
using EnvLookup = std::function<std::optional<std::string>(std::string_view name)>;

// Pure, priority-ordered selection given already-resolved inputs -- no
// environment or file I/O of its own. Multiplexers first (tmux, then
// screen, both unambiguous), then GUI emulators by their own env var.
// konsoleDbusApiEnabled is Konsole's own confirmed-live gate (see
// KonsoleDbusApiEnabledFromConfig below) -- Konsole is skipped outright
// when this is false, rather than opening a tab that can never actually
// launch anything.
[[nodiscard]] std::optional<TerminalKind> SelectTerminal(const EnvLookup& env, bool konsoleDbusApiEnabled);

// Parses konsolerc's own on-disk format directly (never a D-Bus call, so
// checking this never has a side effect): true only if a `[KonsoleWindow]`
// section's `EnableSecuritySensitiveDBusAPI` key is exactly `true`. Missing
// file/section/key defaults to false, matching Konsole's own documented
// default -- confirmed via Konsole's own source
// (Session::isCalledViaDbusAndForbidden) and reproduced live.
[[nodiscard]] bool KonsoleDbusApiEnabledFromConfig(std::string_view konsolercContent);

// The real, production entry point: gathers real getenv values and a real
// konsolerc read, then calls SelectTerminal. Not unit-tested directly, for
// the same reason Clipboard.h's own platform detection isn't -- verified
// live instead (see this file's .cpp header comment).
[[nodiscard]] std::optional<TerminalKind> DetectTerminal();

// argv for every handler except Konsole (a single, independent spawn --
// never through a shell). Konsole needs its own two dependent D-Bus calls
// instead (see below) so it isn't covered by this function.
[[nodiscard]] std::vector<std::string> BuildLaunchArgv(TerminalKind kind, const std::filesystem::path& nedExecutable,
                                                       const std::filesystem::path& root);

// Konsole's own two-step mechanism, confirmed live: step one opens the tab
// in the *calling* window (dbusService/dbusWindow are $KONSOLE_DBUS_SERVICE/
// $KONSOLE_DBUS_WINDOW, which Konsole hands a running session directly --
// no window-index guessing needed) and reports a new session id on stdout;
// step two actually runs ned in it, via the one D-Bus method
// (Session.runCommand) gated by the config setting above. qdbusBinary is
// the resolved qdbus6/qdbus executable name/path -- passed in rather than
// hardcoded so these stay pure and testable regardless of which one a given
// machine actually has installed.
[[nodiscard]] std::vector<std::string> BuildKonsoleNewSessionArgv(std::string_view             qdbusBinary,
                                                                  std::string_view             dbusService,
                                                                  std::string_view             dbusWindow,
                                                                  const std::filesystem::path& root);
[[nodiscard]] std::vector<std::string> BuildKonsoleRunCommandArgv(std::string_view             qdbusBinary,
                                                                  std::string_view             dbusService,
                                                                  std::string_view             sessionId,
                                                                  const std::filesystem::path& nedExecutable,
                                                                  const std::filesystem::path& root);

// The real priority chain: DetectTerminal(), then actually run the
// resulting handler's command(s) via ChildProcess. False if nothing was
// detected, or the detected handler's spawn/D-Bus call failed -- the
// caller (ProjectSwitch.h) falls through to its own next tier in either
// case. Real process spawning, not unit-tested -- verified live instead.
[[nodiscard]] bool TryOpenInNewTab(const std::filesystem::path& nedExecutable, const std::filesystem::path& root);

} // namespace ned::editor

#endif // NED_EDITOR_TERMINALTABLAUNCHER_H
