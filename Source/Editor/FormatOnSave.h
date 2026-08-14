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

namespace ned::editor {

void                                     SetFormatCommand(std::optional<std::string> command);
[[nodiscard]] std::optional<std::string> FormatCommand();

// Runs the configured format command (if any) over text via a temp-file
// pair and a shell invocation (the command reads unformatted text on stdin,
// writes the formatted result to stdout -- the same convention clang-format,
// black, prettier, etc. all already follow standalone). Returns nullopt if
// no command is configured, a temp file couldn't be created, the command
// exits non-zero, or its output is empty -- callers are expected to fall
// back to saving the original, unformatted content in every nullopt case
// rather than risk data loss over a broken formatter.
[[nodiscard]] std::optional<std::string> RunFormatCommand(std::string_view text);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATONSAVE_H
