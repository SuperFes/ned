//
// Where a process for a buffer runs (Docs/ClientServerProtocol.md,
// "Plugging processes in"): beside the buffer's file. A local buffer's runs
// here, as given; a remote buffer's runs on its host, connecting first if
// need be, in the project root found there when no directory is given.
// Everything that starts a process on a buffer's behalf comes through here,
// so where processes go is decided in one place.
//

#ifndef NED_EDITOR_REMOTE_PLACEMENT_H
#define NED_EDITOR_REMOTE_PLACEMENT_H

#include <expected>
#include <functional>
#include <optional>
#include <string>

#include "Editor/Lifetime.h"
#include "Editor/Process/ChildProcess.h"
#include "Editor/Remote/Proc.h"
#include "Text/RemoteFile.h"

namespace ned::editor::remote {

class Hosts;

using Spawned = std::expected<process::ChildProcess, std::string>;

// Main thread only. done runs on the main thread, before this returns for a
// local spawn, and not at all once token has died. hosts may be null when
// nothing remote is possible.
void SpawnBeside(Hosts* hosts, const std::optional<text::RemoteFile>& beside, RemoteSpawn spawn, LifetimeToken token,
                 std::function<void(Spawned spawned)> done);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_PLACEMENT_H
