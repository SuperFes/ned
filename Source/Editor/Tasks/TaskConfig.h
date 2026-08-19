//
// Task runner follow-up. A user-configurable table pointing a task name
// (arbitrary, user-chosen, e.g. "build", "test") at the command+arguments
// run-task should spawn for it.
//
// Mutex-guarded static state, mirroring Lsp/LspServerConfig.h's exact shape
// for its own per-language command map -- same "you install/configure the
// tool, we shell out to it" trust boundary, same "re-registering overwrites,
// empty argv clears" convention.
//

#ifndef NED_EDITOR_TASKS_TASKCONFIG_H
#define NED_EDITOR_TASKS_TASKCONFIG_H

#include <optional>
#include <string>
#include <vector>

namespace ned::editor::tasks {

// Registers argv (argv[0] the executable, remaining elements its arguments,
// e.g. {"cmake", "--build", "."}) as the command run for task name.
// Re-registering overwrites, mirroring CommandRegistry::Register's own
// "expected use, not an error" convention. An empty argv clears any existing
// registration for name.
void SetTaskCommand(const std::string& name, std::vector<std::string> argv);

// std::nullopt if nothing is registered for name -- not an error; TaskRunner
// treats this as "no such task configured," reporting it in the task's
// output buffer rather than crashing.
[[nodiscard]] std::optional<std::vector<std::string>> TaskCommand(const std::string& name);

} // namespace ned::editor::tasks

#endif // NED_EDITOR_TASKS_TASKCONFIG_H
