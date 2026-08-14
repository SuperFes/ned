//
// XDG-compliant loading of the user's Janet init file (the ned/init.el
// equivalent). See CLAUDE.md's XDG convention note.
//

#ifndef NED_JANET_INITFILE_H
#define NED_JANET_INITFILE_H

#include <filesystem>

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

} // namespace ned::janet

#endif // NED_JANET_INITFILE_H
