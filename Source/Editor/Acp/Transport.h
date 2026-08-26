//
// ACP client, slice 1. ACP's own message framing -- newline-delimited JSON,
// one message per line, no "Content-Length" header block at all (unlike
// Lsp/Transport.h/Dap's shared framing) -- layered directly on
// Process/ChildProcess.h's raw spawn/pipe mechanics, exactly the reuse that
// file's own header comment anticipated when it split ChildProcess out from
// the original LSP-only Transport. This is its own class rather than a reuse
// of Lsp::Transport because the framing genuinely differs, not because ACP
// needs different process/pipe handling.
//
// A JSON-RPC payload produced by nlohmann::json::dump() never contains a
// literal, unescaped newline byte (one embedded in a string is always
// escaped as "\n" during serialization) -- so a message body is always
// exactly one line, safe to frame this way.
//

#ifndef NED_EDITOR_ACP_TRANSPORT_H
#define NED_EDITOR_ACP_TRANSPORT_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/ProcessTimeouts.h"

namespace ned::editor::acp {

// subprocess-hang-protection follow-up -- see Lsp/Transport.h's identical
// stall-timeout reasoning; ACP's messages are single lines, not multi-line
// frames, but the same "silence is normal between messages, never
// mid-message" policy applies. ChildProcess-hang-protection-round-2
// follow-up: the real, no-argument call site below now reads
// ProcessTimeouts.h's Janet-configurable ProtocolStallTimeoutMs() (the same
// shared setting LSP's Transport uses) instead of this file's old
// kMessageStallTimeout compile-time constant.

class Transport {
  public:
    // argv[0] is resolved against $PATH (or treated as a literal path if it
    // contains a '/', matching execvp's own convention) before spawning.
    // Throws std::runtime_error if argv is empty, the executable can't be
    // resolved/isn't executable, pipe creation fails, or posix_spawn itself
    // fails synchronously.
    //
    // lsp-stderr-capture follow-up (extended to ACP): captureStderr mirrors
    // Lsp/Transport.h's identical parameter exactly -- defaults to false so
    // nothing changes for a caller that doesn't ask.
    explicit Transport(const std::vector<std::string>& argv, bool captureStderr = false);

    // Wraps already-open, already-connected file descriptors directly,
    // taking ownership of both -- for a caller that manages the underlying
    // connection itself (a test driving a raw pipe pair with no real
    // subprocess involved). pid, if given, is reaped/killed by the
    // destructor the same way the process-spawning constructor's child is;
    // -1 (the default) means "no process to manage."
    Transport(int readFd, int writeFd, pid_t pid = -1) noexcept;

    ~Transport() = default; // ChildProcess's own destructor does the real work

    Transport(Transport&&)                 = default;
    Transport& operator=(Transport&&)      = default;
    Transport(const Transport&)            = delete;
    Transport& operator=(const Transport&) = delete;

    // Writes one message (jsonPayload + "\n") to the child's stdin. Throws
    // std::runtime_error on a write failure (e.g. the child already exited
    // and closed its stdin -- EPIPE) or (write-side-hang-protection
    // follow-up) if the child stops draining its stdin for longer than
    // stallTimeout -- same rationale/default as ReadMessage's own
    // stallTimeout parameter below.
    void WriteMessage(std::string_view jsonPayload, std::chrono::milliseconds stallTimeout = ProtocolStallTimeoutMs()) const;

    // Blocks until one full line has been read from the child's stdout,
    // returning it with the trailing newline stripped. Returns std::nullopt
    // on EOF (the agent exited) -- an ordinary, expected outcome a caller
    // needs to detect and react to, not an exceptional one. A blank line
    // (the agent writing a bare "\n", e.g. as a keepalive) is returned as an
    // empty string, not treated as EOF -- callers skip it rather than this
    // layer guessing at agent-specific keepalive conventions. Throws
    // std::runtime_error (subprocess-hang-protection follow-up) if a message
    // stalls mid-line for longer than stallTimeout -- a parameter, not a
    // hardcoded sleep, purely so tests can shorten it; real callers always
    // take the ProtocolStallTimeoutMs() default (see this file's own header
    // comment).
    [[nodiscard]] std::optional<std::string> ReadMessage(std::chrono::milliseconds stallTimeout = ProtocolStallTimeoutMs()) const;

    [[nodiscard]] pid_t Pid() const noexcept;

    // lsp-stderr-capture follow-up (extended to ACP): see Lsp/Transport.h's
    // identical StderrFd() doc comment.
    [[nodiscard]] int StderrFd() const noexcept;

    // lsp-stderr-capture follow-up (extended to ACP): see Lsp/Transport.h's
    // identical ProcessLabel() doc comment.
    [[nodiscard]] const std::string& ProcessLabel() const noexcept;

  private:
    process::ChildProcess child_;
    std::string           processLabel_;
};

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_TRANSPORT_H
