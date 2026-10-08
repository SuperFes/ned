//
// The external formatter command run over a buffer's content on save (see
// save-buffer in Commands.cpp), if one has been configured. One process-wide
// command, not per-mode/per-extension -- a deliberate v1 scope cut mirroring
// how Theme selection is a single process-wide choice too, not per-buffer.
// Unset (the default) means save-buffer never formats; nothing built-in ever
// sets one -- it's configured from Janet (ned/set-format-command).
//

#ifndef NED_EDITOR_FORMATONSAVE_H
#define NED_EDITOR_FORMATONSAVE_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

void                                     SetFormatCommand(std::optional<std::string> command);
[[nodiscard]] std::optional<std::string> FormatCommand();

// The command as a process runs it, here or on a remote buffer's host.
[[nodiscard]] std::vector<std::string> FormatCommandArgv(const std::string& command);

// What a run of the command formats a buffer to: its stdout, unless it
// exited non-zero or printed nothing. Callers save the original content in
// every nullopt case rather than risk data loss over a broken formatter.
[[nodiscard]] std::optional<std::string> AcceptFormatOutput(std::string output, std::optional<int> exitCode);

// Runs the configured format command over text, here: the command reads
// unformatted text on stdin and writes the result to stdout, as
// clang-format, black, prettier, etc. already do standalone. nullopt when
// no command is configured, it can't start, or AcceptFormatOutput refuses
// what it printed.
[[nodiscard]] std::optional<std::string> RunFormatCommand(std::string_view text);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATONSAVE_H
