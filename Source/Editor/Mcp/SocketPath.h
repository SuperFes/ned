//
// ACP MCP tool-server bridge, slice 1. Filesystem location for the live `ned`
// process's own MCP bridge socket -- one per interactive `ned` instance
// (keyed by pid), not one per machine like Lsp/BrokerSocketPath.h's LSP
// broker daemon: each running `ned` has its own distinct live state
// (open buffers, LspManager/VcsRunner/TestRunner connections), so there is
// nothing to multiplex across processes here. Deliberately a self-contained
// duplicate of BrokerSocketPath.h's directory-resolution logic rather than a
// cross-namespace reuse of it -- this codebase's established precedent for a
// small per-subsystem primitive (see Lsp/Transport.h vs. Acp/Transport.h,
// independently duplicated rather than shared despite similar shape).
//

#ifndef NED_EDITOR_MCP_SOCKETPATH_H
#define NED_EDITOR_MCP_SOCKETPATH_H

#include <filesystem>

namespace ned::editor::mcp {

// $XDG_RUNTIME_DIR/ned, falling back to $XDG_STATE_HOME/ned/run, falling
// back to $HOME/.local/state/ned/run if neither is set. Throws
// std::runtime_error if none is usable. Pure path calculation -- does not
// create the directory; see EnsureRuntimeDirectory below for that.
[[nodiscard]] std::filesystem::path RuntimeDirectory();

// Creates RuntimeDirectory() (and any missing parents) if it doesn't
// already exist, with permissions restricted to the owner (0700) -- a
// world-readable directory would let another local user on a shared machine
// discover and connect to this process's live editing state. Safe to call
// repeatedly. Throws std::runtime_error on failure.
void EnsureRuntimeDirectory();

// RuntimeDirectory() / "mcp-<pid>.sock" -- this `ned` process's own MCP
// bridge socket, listened on by mcp::BridgeServer and connected to by the
// `ned --mcp-stdio-relay` subprocess the ACP agent spawns as its configured
// stdio MCP server.
[[nodiscard]] std::filesystem::path SocketPathForPid(int pid);

} // namespace ned::editor::mcp

#endif // NED_EDITOR_MCP_SOCKETPATH_H
