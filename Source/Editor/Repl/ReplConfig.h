//
// REPL-engine follow-up. A user-configurable table pointing a REPL name
// (arbitrary, user-chosen, e.g. "python", "php") at the command+arguments
// run-repl should spawn for it -- the language's own interactive CLI REPL,
// run on a real pty (see UI/TerminalPanel.h) exactly as if launched in a
// terminal: no send-region/prompt-detection layer, the language's own
// readline/completion/history/coloring does all of that itself.
//
// Mutex-guarded static state, mirroring Tasks/TaskConfig.h's exact shape --
// same "you install/configure the tool, we shell out to it" trust boundary,
// same "re-registering overwrites, empty argv clears" convention. Janet
// (ned's own built-in REPL) is deliberately not configured here at all --
// see UI/JanetReplPanel.h -- it evaluates in-process against the live
// janet::Environment rather than spawning a subprocess.
//

#ifndef NED_EDITOR_REPL_REPLCONFIG_H
#define NED_EDITOR_REPL_REPLCONFIG_H

#include <optional>
#include <string>
#include <vector>

namespace ned::editor::repl {

// Registers argv (argv[0] the executable, remaining elements its arguments,
// e.g. {"python3", "-i"}) as the command run for REPL name. Re-registering
// overwrites. An empty argv clears any existing registration for name.
void SetReplCommand(const std::string& name, std::vector<std::string> argv);

// std::nullopt if nothing is registered for name -- not an error; run-repl
// reports it via the shared status message rather than crashing.
[[nodiscard]] std::optional<std::vector<std::string>> ReplCommand(const std::string& name);

} // namespace ned::editor::repl

#endif // NED_EDITOR_REPL_REPLCONFIG_H
