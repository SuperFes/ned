//
// Named-projects follow-up: the real "activate this project root" priority
// chain switch-project/open-project (BufferView, a later follow-up) drive
// once the user has picked or newly opened one --
//   1. A detected terminal/multiplexer opens root as a sibling process in a
//      new tab/window (TerminalTabLauncher.h) -- current process untouched.
//   2. Else, a user-configured custom command (ned/set-project-open-command)
//      -- also untouched.
//   3. Else, replace this process in place: verify a re-exec is even
//      possible *before* asking the caller to quit (PendingReExec.h), so a
//      switch that can't complete never begins tearing anything down.
//
// Deliberately UI-free (Editor/ has no dependency on UI/) -- ActivateProjectRoot
// never calls into EventLoop/BufferView itself. Outcome::ReplacingInPlace
// means PendingReExec has already been set; the caller (BufferView, which
// already holds an EventLoop*) is what actually calls eventLoop_->Exit(),
// the exact same call HandleConfirmQuitKey's 'y' branch already makes.
//

#ifndef NED_EDITOR_PROJECTSWITCH_H
#define NED_EDITOR_PROJECTSWITCH_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ned::editor {

enum class ProjectActivationOutcome {
    OpenedInNewTab,   // TerminalTabLauncher succeeded -- current process untouched
    RanCustomCommand, // ned/set-project-open-command succeeded -- current process untouched
    ReplacingInPlace, // PendingReExec is set; caller must now quit
    Failed,           // Nothing worked; current process untouched, nothing pending
    // A registered/typed root that no longer exists on disk (deleted,
    // renamed, an unmounted drive, ...) -- checked and reported up front,
    // before ever touching the registry or trying any tier: without this,
    // every tier would either silently fail (new-tab/custom-command spawn a
    // command against a nonexistent -c/--cwd) or, worse, replace-in-place
    // would *succeed* and hand the re-exec'd ned a nonexistent path, which
    // main.cpp's own CLI handling treats as "create a new file here" --
    // quietly creating a stray file at a deleted project's old location
    // instead of reporting anything wrong.
    RootMissing,
};

// Pure: replaces every "{root}" occurrence in each argv element with root's
// own string form -- never a shell, each element substituted independently,
// mirroring TestRunConfig.h's own placeholder-substitution convention.
[[nodiscard]] std::vector<std::string> SubstituteProjectOpenCommandArgv(const std::vector<std::string>& argvTemplate,
                                                                        const std::filesystem::path&    root);

// -- Process-wide setting (mutex-guarded static state) ------------------------

// The escape hatch for a terminal TerminalTabLauncher doesn't auto-detect,
// or a user's own preferred invocation. argv[0] is resolved against $PATH
// (ChildProcess's own convention); an empty argv clears it, same convention
// every other ned/set-*-command Janet binding uses.
void                                                  SetProjectOpenCommand(std::vector<std::string> argvTemplate);
[[nodiscard]] std::optional<std::vector<std::string>> ProjectOpenCommand();

// The real priority chain. Always touches root's project-registry entry
// first, if one exists (ProjectRegistry.h) -- switching to it is itself a
// "use," regardless of which mechanism ends up doing the actual work.
[[nodiscard]] ProjectActivationOutcome ActivateProjectRoot(const std::filesystem::path& root);

// Tests only.
void ResetProjectOpenCommandForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTSWITCH_H
