//
// lsp-broker follow-up. Filesystem locations for the LSP broker daemon's
// Unix domain socket and its startup-race lock file -- a single,
// well-known pair (not one per project; see Broker.h's own header
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

// Editor/RuntimeDir.h's RuntimeDirectory(): the broker's files sit at its
// top level. A pure path calculation.
[[nodiscard]] std::filesystem::path BrokerRuntimeDirectory();

// EnsureRuntimeDirectory(): creates BrokerRuntimeDirectory() owner-only
// (0700), so another local user can't find and connect to the broker's
// socket. Throws std::runtime_error on failure.
void EnsureBrokerRuntimeDirectory();

// BrokerRuntimeDirectory() / "broker.sock" -- the daemon's single Unix
// domain socket, listened on by BrokerMain.cpp and connected to by
// every `ned` process's Manager.
[[nodiscard]] std::filesystem::path BrokerSocketPath();

// BrokerRuntimeDirectory() / "broker.lock" -- flock()'d for the whole
// check-connect-fail -> fork/exec -> poll-until-connectable window when a
// `ned` process is about to become the daemon's first spawner, so two
// racing `ned` launches can't both try to bind the listening socket.
// flock() releases automatically if the holding process dies, so no
// separate stale-lock cleanup is needed.
[[nodiscard]] std::filesystem::path BrokerLockPath();

// broker-log-redirect follow-up. BrokerRuntimeDirectory() / "broker.log" -- where
// BrokerMain.cpp's own timestamped Log() writes once TryBecomeBrokerSpawner
// (BrokerConnect.cpp) redirects the daemon's stdout/stderr here right after fork(),
// before exec. A manually-launched `ned --lsp-broker` still inherits its caller's own
// stdout/stderr unmodified (that redirect only happens in the auto-spawn path) -- this
// is just where the auto-spawned daemon's log ends up so it stops leaking into whichever
// terminal happened to trigger the very first LSP connection after a fresh boot/login.
[[nodiscard]] std::filesystem::path BrokerLogPath();

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_BROKERSOCKETPATH_H
