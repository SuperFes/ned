//
// XDG-compliant loading of the user's Janet init file (the ned/init.el
// equivalent). See CLAUDE.md's XDG convention note.
//

#ifndef NED_JANET_INITFILE_H
#define NED_JANET_INITFILE_H

#include <filesystem>
#include <string>
#include <string_view>

#include "Environment.h"

namespace ned::janet {

// $XDG_CONFIG_HOME/ned/init.janet, falling back to $HOME/.config/ned/init.janet
// if XDG_CONFIG_HOME is unset or empty. Throws std::runtime_error if neither
// is usable.
[[nodiscard]] std::filesystem::path InitFilePath();

// Evaluates InitFilePath() in env if it exists; silently does nothing if it
// doesn't (no init file is a normal, expected state, not an error).
// Propagates Environment::DoFile's exception for any other failure (e.g. a
// Janet-level error inside the file).
void LoadInitFile(Environment& env);

// The select-theme picker's "write it down" half: given an init.janet's
// current text, returns it with `(ned/set-theme "<name>")` in effect.
//
// Pure and unit-tested, because rewriting somebody's config is the part that
// has to be predictable. The rule is deliberately narrow: it replaces the
// *last* line that is exactly a `(ned/set-theme "literal")` call and nothing
// else, and otherwise appends one. Anything cleverer -- a call inside a
// conditional, one sharing a line with other code -- is left alone and gets
// an appended call instead, which is correct rather than merely safe: a
// later ned/set-theme wins, matching Janet's own sequential evaluation, so
// the appended line is what takes effect either way.
//
// Never reformats, never reorders, and never touches a line it did not
// match. A trailing newline is added only if the file lacked one.
[[nodiscard]] std::string WithSetThemeCall(std::string_view initFileText, std::string_view themeName);

// WithSetThemeCall applied to InitFilePath(), created if absent. Writes via
// a sibling .ned-tmp + rename, preserving the file's existing permissions
// (Text/FilePreservation.h's rule -- a rename replaces the inode, so mode
// bits vanish unless put back). Throws std::runtime_error on any I/O
// failure, and on a path that cannot be resolved.
void WriteSetThemeCall(std::string_view themeName);

} // namespace ned::janet

#endif // NED_JANET_INITFILE_H
