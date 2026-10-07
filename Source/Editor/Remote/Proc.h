//
// A host's processes as ChildProcesses (Docs/ClientServerProtocol.md,
// "Plugging processes in"). proc/spawn runs one on the host, and a relay
// moves bytes between its streams and local socketpairs the returned
// ChildProcess wraps, so whatever reads a local child reads this one the
// same way: framing layers, StreamedChild, TaskProcess.
//
// The relay is a thread per process, owned by the child. Output is held
// against the host's credit until the child's reader has taken it; stdin
// goes up one write at a time. The child's exit is the spawn's answer, which
// only the main thread receives, so a relayed child is never waited for
// there: its Kill and destructor signal the host and return.
//

#ifndef NED_EDITOR_REMOTE_PROC_H
#define NED_EDITOR_REMOTE_PROC_H

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Remote/Connection.h"

namespace ned::editor::remote {

struct RemoteSpawn {
    std::vector<std::string>                                        argv;
    std::optional<std::filesystem::path>                            cwd; // the host's; its login's home when unset
    std::vector<std::pair<std::string, std::optional<std::string>>> env;
    process::StderrMode                                             stderrMode = process::StderrMode::Discard;
};

// The exit status of a command the host couldn't start, as a shell reports
// one it can't find. Why goes to its stderr, or its stdout when merged.
inline constexpr int kRemoteSpawnFailed = 127;

// Main thread only. Throws std::runtime_error when the connection has
// closed or its agent can't run processes.
[[nodiscard]] process::ChildProcess SpawnOnHost(Connection& connection, RemoteSpawn spawn);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_PROC_H
