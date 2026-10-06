//
// Where ned keeps live, per-user runtime handles: sockets and lock files
// that mean nothing once the processes using them are gone. XDG_RUNTIME_DIR
// is the spec's place for these; the state directory stands in when a
// session has none.
//

#ifndef NED_EDITOR_RUNTIMEDIR_H
#define NED_EDITOR_RUNTIMEDIR_H

#include <filesystem>
#include <string_view>

namespace ned::editor {

// $XDG_RUNTIME_DIR/ned, falling back to $XDG_STATE_HOME/ned/run, then
// $HOME/.local/state/ned/run. Throws std::runtime_error if none of the
// three is set. A pure path calculation.
[[nodiscard]] std::filesystem::path RuntimeDirectory();

// Creates RuntimeDirectory(), and subdirectory beneath it when given, each
// restricted to the owner (0700): another local user who can list these
// can connect to the sockets inside. Returns the innermost directory. Safe
// to call repeatedly. Throws std::runtime_error on failure.
std::filesystem::path EnsureRuntimeDirectory(std::string_view subdirectory = {});

} // namespace ned::editor

#endif
