//
// ACP MCP tool-server bridge, slice 1. MCP's own stdio wire framing --
// newline-delimited JSON-RPC 2.0, one message per line, no "Content-Length"
// header block (identical in shape to Editor/Acp/Transport.h's own ACP
// framing, and for the same reason: a nlohmann::json::dump() payload never
// contains a literal unescaped newline, so a message body is always exactly
// one line). Deliberately its own class rather than a reuse of
// Acp::Transport -- this codebase's established precedent
// (Lsp::Transport/Acp::Transport are independent classes despite similar
// shape) is that each protocol gets its own Transport wrapping
// Process/ChildProcess.h directly, so Editor/Mcp/ has no dependency on
// Editor/Acp/ at all.
//
// Unlike Lsp::Transport/Acp::Transport, this class's *primary* construction
// path is the raw-fd constructor, not the argv-spawning one: the MCP bridge
// server lives inside the live `ned` process and talks to an accept()ed
// Unix domain socket connection, never spawns a subprocess of its own. The
// argv-spawning constructor exists only for symmetry/tests.
//

#ifndef NED_EDITOR_MCP_MCPTRANSPORT_H
#define NED_EDITOR_MCP_MCPTRANSPORT_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/ProcessTimeouts.h"

namespace ned::editor::mcp {

class Transport {
  public:
    // argv[0] is resolved against $PATH (or treated as a literal path if it
    // contains a '/', matching execvp's own convention) before spawning.
    // Throws std::runtime_error if argv is empty, the executable can't be
    // resolved/isn't executable, pipe creation fails, or posix_spawn itself
    // fails synchronously. Not the real construction path in production --
    // see this file's own header comment.
    explicit Transport(const std::vector<std::string>& argv);

    // Wraps already-open, already-connected file descriptors directly,
    // taking ownership of both -- the real construction path: readFd/writeFd
    // are typically the same accept()ed socket fd and a dup() of it (a
    // single bidirectional socket, framed as two logical directions the same
    // way `ned --lsp-broker-stop`'s own one-shot socket write already does
    // in main.cpp), or two ends of a pipe pair in a test. pid, if given, is
    // reaped/killed by the destructor the same way the process-spawning
    // constructor's child is; -1 (the default) means "no process to manage."
    Transport(int readFd, int writeFd, pid_t pid = -1) noexcept;

    ~Transport() = default; // ChildProcess's own destructor does the real work

    Transport(Transport&&)                 = default;
    Transport& operator=(Transport&&)      = default;
    Transport(const Transport&)            = delete;
    Transport& operator=(const Transport&) = delete;

    // Writes one message (jsonPayload + "\n"). Throws std::runtime_error on a
    // write failure (e.g. the peer already closed its end -- EPIPE) or
    // (write-side-hang-protection precedent, see Lsp/Acp Transport) if the
    // peer stops draining for longer than stallTimeout.
    void WriteMessage(std::string_view jsonPayload, std::chrono::milliseconds stallTimeout = ProtocolWriteStallTimeoutMs()) const;

    // Blocks until one full line has been read, returning it with the
    // trailing newline stripped. Returns std::nullopt on EOF (the peer
    // disconnected) -- an ordinary, expected outcome, not exceptional. A
    // blank line is returned as an empty string, not treated as EOF. Throws
    // std::runtime_error if a message stalls mid-line for longer than
    // stallTimeout.
    [[nodiscard]] std::optional<std::string> ReadMessage(std::chrono::milliseconds stallTimeout = ProtocolReadStallTimeoutMs()) const;

    [[nodiscard]] pid_t Pid() const noexcept;

  private:
    process::ChildProcess child_;
};

} // namespace ned::editor::mcp

#endif // NED_EDITOR_MCP_MCPTRANSPORT_H
