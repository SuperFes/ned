//
// A child on a fresh pseudo-terminal, for a terminal application: it needs
// a controlling tty and session leadership, which posix_spawn cannot give
// it, so this is forkpty and execve. The master side is then adopted by a
// ChildProcess as (dup(master), master), which supplies the reads, writes
// and teardown unchanged -- closing the master is what hangs the session
// up. stdout and stderr are the same terminal, so there is no stderr of its
// own, and no EOF to send its input either.
//

#ifndef NED_EDITOR_PROCESS_PTY_H
#define NED_EDITOR_PROCESS_PTY_H

#include <string>
#include <vector>

#include "Editor/Process/ChildProcess.h"

namespace ned::editor::process {

// Spawns argv on a pty of the given size, in options.cwd with options.env
// applied; options.stderrMode is ignored. Throws std::runtime_error if argv
// is empty, argv[0] can't be resolved, the cwd isn't a directory, or the
// pty or fork fails. An exec that fails anyway exits 127.
[[nodiscard]] ChildProcess SpawnOnPty(const std::vector<std::string>& argv, TerminalSize size, const SpawnOptions& options);

} // namespace ned::editor::process

#endif // NED_EDITOR_PROCESS_PTY_H
