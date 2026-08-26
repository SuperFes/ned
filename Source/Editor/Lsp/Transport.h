//
// LSP client follow-up. LSP's own JSON-RPC framing ("Content-Length: N\r\n\r\n"
// + payload) layered on top of Process/ChildProcess.h's raw spawn/pipe
// mechanics -- no JSON-RPC/LSP *semantics* here at all (see LspClient.h for
// that layer), just the framing. ChildProcess itself was extracted out of
// this file (task-runner follow-up) once a second, framing-free consumer
// (TaskProcess, streaming raw build/test output) needed the same spawn/pipe
// mechanics without any framing at all -- see ChildProcess.h's own header
// comment for the fuller reasoning, including why this split also sets up a
// future ACP client to layer its own framing on ChildProcess the same way
// this class does.
//

#ifndef NED_EDITOR_LSP_TRANSPORT_H
#define NED_EDITOR_LSP_TRANSPORT_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/ProcessTimeouts.h"

namespace ned::editor::lsp {

// subprocess-hang-protection follow-up. Once a frame has started arriving,
// further silence is always anomalous (a connection legitimately idles
// *between* frames, never mid-frame) -- ReadFrame bounds every read after a
// frame's first byte by this, so a server that starts sending and then
// wedges mid-message eventually surfaces as a reported disconnect instead of
// leaking the read loop forever. Generous on purpose: never meant to fire
// against a real, working server, only a genuinely stuck one.
//
// ChildProcess-hang-protection-round-2 follow-up: the real, no-argument call
// site now reads ProcessTimeouts.h's Janet-configurable
// ProtocolStallTimeoutMs() (default 30000ms, same value the old
// kFrameStallTimeout constant this replaced always had) instead of a fixed
// compile-time constant -- ReadFrame's test-only override parameter below
// still lets a test pass its own short value directly, bypassing this
// default entirely.

class Transport {
  public:
    // argv[0] is resolved against $PATH (or treated as a literal path if it
    // contains a '/', matching execvp's own convention) before spawning.
    // Throws std::runtime_error if argv is empty, the executable can't be
    // resolved/isn't executable, pipe creation fails, or posix_spawn itself
    // fails synchronously.
    //
    // lsp-stderr-capture follow-up: captureStderr defaults to false,
    // preserving every existing caller's behavior unchanged (most notably
    // DapClient, which reuses this exact constructor and has no reader for
    // a captured stderr pipe -- see StderrFd()'s own doc comment on what an
    // undrained Capture pipe risks). LspClient's real-subprocess constructor
    // is the one caller that passes true.
    explicit Transport(const std::vector<std::string>& argv, bool captureStderr = false);

    // Wraps already-open, already-connected file descriptors directly,
    // taking ownership of both -- for a caller that manages the underlying
    // connection itself (a test driving a raw pipe pair with no real
    // subprocess involved; in principle also a future non-stdio connection,
    // e.g. a socket). pid, if given, is reaped/killed by the destructor the
    // same way the process-spawning constructor's child is; -1 (the
    // default) means "no process to manage," skipping that logic entirely.
    Transport(int readFd, int writeFd, pid_t pid = -1) noexcept;

    ~Transport() = default; // ChildProcess's own destructor does the real work

    Transport(Transport&&)                 = default;
    Transport& operator=(Transport&&)      = default;
    Transport(const Transport&)            = delete;
    Transport& operator=(const Transport&) = delete;

    // Writes one LSP frame ("Content-Length: N\r\n\r\n" + payload) to the
    // child's stdin. Throws std::runtime_error on a write failure (e.g. the
    // child already exited and closed its stdin -- EPIPE) or
    // (write-side-hang-protection follow-up) if the child stops draining its
    // stdin for longer than stallTimeout -- same rationale/default as
    // ReadFrame's own stallTimeout parameter below.
    void WriteFrame(std::string_view jsonPayload, std::chrono::milliseconds stallTimeout = ProtocolStallTimeoutMs()) const;

    // Blocks until one full LSP frame has been read from the child's
    // stdout. Returns std::nullopt on EOF (the server exited) rather than
    // throwing -- that's an ordinary, expected outcome a caller needs to
    // detect and react to, not an exceptional one. Throws std::runtime_error
    // if a frame is malformed (missing/unparseable Content-Length) or
    // (subprocess-hang-protection follow-up) if it stalls mid-frame for
    // longer than stallTimeout -- a parameter, not a hardcoded sleep, purely
    // so tests can shorten it; real callers always take the
    // ProtocolStallTimeoutMs() default (see this file's own header comment).
    [[nodiscard]] std::optional<std::string> ReadFrame(std::chrono::milliseconds stallTimeout = ProtocolStallTimeoutMs()) const;

    [[nodiscard]] pid_t Pid() const noexcept;

    // lsp-stderr-capture follow-up. The read end of the child's stderr pipe
    // when this Transport was constructed with captureStderr = true; -1
    // otherwise (the default, or the raw-fd constructor, which has no
    // subprocess of its own to capture from). A caller reads this directly
    // with its own ::read() loop -- see ChildProcess::StderrFd()'s own doc
    // comment for why no buffered-reader wrapper exists here either.
    [[nodiscard]] int StderrFd() const noexcept;

    // lsp-stderr-capture follow-up. argv[0]'s basename, captured at
    // construction -- lets a stderr line be tagged with which server
    // process produced it (e.g. "clangd: ...") without LspClient/LspManager
    // needing to thread a language name down into this layer. Empty for the
    // raw-fd constructor (no argv was ever given).
    [[nodiscard]] const std::string& ProcessLabel() const noexcept;

  private:
    process::ChildProcess child_;
    std::string           processLabel_;
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_TRANSPORT_H
