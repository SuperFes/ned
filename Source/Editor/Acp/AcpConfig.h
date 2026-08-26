//
// ACP client, slice 1. A user-configurable table pointing an agent name
// (arbitrary, user-chosen, e.g. "claude-code") at the command+arguments to
// launch it -- an ACP agent isn't tied to a buffer's language the way an LSP
// server is, so this is keyed like Tasks/TaskConfig.h's task-name table, not
// Lsp/LspServerConfig.h's per-language one.
//
// Mutex-guarded static state, same "you install/configure the tool, we shell
// out to it" trust boundary and "re-registering overwrites, empty argv
// clears" convention as every sibling config table in this codebase.
//

#ifndef NED_EDITOR_ACP_ACPCONFIG_H
#define NED_EDITOR_ACP_ACPCONFIG_H

#include <optional>
#include <string>
#include <vector>

namespace ned::editor::acp {

// Registers argv (argv[0] the executable, remaining elements its arguments,
// e.g. {"claude-code-acp"}) as the command run to spawn the agent named
// name. Re-registering overwrites. An empty argv clears any existing
// registration for name.
void SetAcpAgentCommand(const std::string& name, std::vector<std::string> argv);

// std::nullopt if nothing is registered for name -- not an error; AcpManager
// treats this as "no such agent configured," reporting it rather than
// crashing.
[[nodiscard]] std::optional<std::vector<std::string>> AcpAgentCommand(const std::string& name);

// Every currently-registered agent name, sorted -- ACP round-1-live-
// validation follow-up: lets the "ACP agent:" prompt Tab-complete instead of
// staying pure free text, the same way VcsSwitchBranch completes against a
// fetched branch list. Cheap enough to call fresh on every keystroke (a
// mutex-guarded in-memory map lookup, not an async fetch).
[[nodiscard]] std::vector<std::string> AcpAgentNames();

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_ACPCONFIG_H
