//
// lsp-broker follow-up. Filesystem locations for the LSP broker daemon's
// Unix domain socket and its startup-race lock file -- a single,
// well-known pair (not one per project; see LspBroker.h's own header
// comment for why one daemon now multiplexes every project). Pure path
// calculation, mirroring Backup.h/Session.h's own "calculate, don't
// create" split -- BrokerRuntimeDirectory() is the one function here that
// actually touches the filesystem, and only to create the directory
// itself with restrictive permissions.
//

#ifndef NED_EDITOR_LSP_BROKERSOCKETPATH_H
#define NED_EDITOR_LSP_BROKERSOCKETPATH_H

#include <filesystem>

namespace ned::editor::lsp {

// $XDG_RUNTIME_DIR/ned, falling back to $XDG_STATE_HOME/ned/run, falling
// back to $HOME/.local/state/ned/run if neither is set. Throws
// std::runtime_error if none is usable. XDG_RUNTIME_DIR is the spec's own
// answer for exactly this kind of ephemeral, per-user, tmpfs-backed
// runtime file (sockets, lock files) -- the first use of that variable
// anywhere in this codebase, everything else so far being disposable
// *state* (Session.h/Backup.h/ProjectSession.h/...) rather than a live
// runtime handle. A pure path calculation -- does not create the
// directory; see EnsureBrokerRuntimeDirectory below for that.
[[nodiscard]] std::filesystem::path BrokerRuntimeDirectory();

// Creates BrokerRuntimeDirectory() (and any missing parents) if it doesn't
// already exist, with permissions restricted to the owner (0700) --
// deliberately stricter than every other XDG directory helper in this
// codebase (none of which pass an explicit mode to create_directories at
// all), since a world-readable directory would let another local user on
// a shared machine discover and connect to the broker's socket. Safe to
// call repeatedly; a no-op if the directory already exists with the
// right permissions. Throws std::runtime_error on failure.
void EnsureBrokerRuntimeDirectory();

// BrokerRuntimeDirectory() / "broker.sock" -- the daemon's single Unix
// domain socket, listened on by LspBrokerMain.cpp and connected to by
// every `ned` process's LspManager.
[[nodiscard]] std::filesystem::path BrokerSocketPath();

// BrokerRuntimeDirectory() / "broker.lock" -- flock()'d for the whole
// check-connect-fail -> fork/exec -> poll-until-connectable window when a
// `ned` process is about to become the daemon's first spawner, so two
// racing `ned` launches can't both try to bind the listening socket.
// flock() releases automatically if the holding process dies, so no
// separate stale-lock cleanup is needed.
[[nodiscard]] std::filesystem::path BrokerLockPath();

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_BROKERSOCKETPATH_H
