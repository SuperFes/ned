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

namespace ned::editor::acp {

// subprocess-hang-protection follow-up -- see Lsp/Transport.h's identical
// kFrameStallTimeout constant/reasoning; ACP's messages are single lines, not
// multi-line frames, but the same "silence is normal between messages, never
// mid-message" policy applies.
inline constexpr std::chrono::milliseconds kMessageStallTimeout{30000};

class Transport {
  public:
    // argv[0] is resolved against $PATH (or treated as a literal path if it
    // contains a '/', matching execvp's own convention) before spawning.
    // Throws std::runtime_error if argv is empty, the executable can't be
    // resolved/isn't executable, pipe creation fails, or posix_spawn itself
    // fails synchronously.
    explicit Transport(const std::vector<std::string>& argv);

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
    // and closed its stdin -- EPIPE).
    void WriteMessage(std::string_view jsonPayload) const;

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
    // take the kMessageStallTimeout default.
    [[nodiscard]] std::optional<std::string> ReadMessage(std::chrono::milliseconds stallTimeout = kMessageStallTimeout) const;

    [[nodiscard]] pid_t Pid() const noexcept;

  private:
    process::ChildProcess child_;
};

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_TRANSPORT_H
