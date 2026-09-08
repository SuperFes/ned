//
// Task runner follow-up (extracted out of Lsp/Transport.h). Raw process +
// pipe mechanics for spawning a long-lived subprocess and talking to it over
// stdio -- no framing/protocol semantics here at all. This is the shared
// primitive Transport (LSP's own Content-Length framing) and TaskProcess
// (raw, unframed byte streaming) both build on, so a future ACP client can
// layer its own JSON-RPC framing on top of this exact same class the day it's
// needed, the same way Transport does today -- see this class's own
// ROADMAP.md entry for the reasoning.
//
// Spawns via posix_spawn, not fork+exec, and does its own $PATH resolution
// before spawning -- see Transport.h's original header comment (still
// accurate) for exactly why; that reasoning moved here unchanged along with
// the code.
//

#ifndef NED_EDITOR_PROCESS_CHILDPROCESS_H
#define NED_EDITOR_PROCESS_CHILDPROCESS_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace ned::editor::process {

// Discard (the LSP/DAP default -- server logs go nowhere), MergeWithStdout
// (dup2'd onto the same fd as stdout, i.e. shell `2>&1` -- what a task
// runner wants, since a build/test tool's error output belongs in the same
// stream as its normal output), or Capture (lsp-stderr-capture follow-up: a
// third pipe, kept separate from stdout so it never corrupts a framed
// protocol's own stream -- StderrFd() exposes the read end for a caller that
// wants to actually drain and log it; a caller that requests this mode and
// then never reads StderrFd() risks the child blocking once the pipe's
// kernel buffer fills, same risk any unread pipe carries).
enum class StderrMode { Discard,
                        MergeWithStdout,
                        Capture };

// Resolves a command name against $PATH (or validates it directly if it
// contains a '/', execvp's own convention), returning the runnable path or
// std::nullopt. Shared by ChildProcess's own spawn and by
// Terminal/PtyProcess, whose post-forkpty execve needs the resolution done
// before forking (a $PATH walk isn't async-signal-safe).
[[nodiscard]] std::optional<std::string> ResolveExecutable(const std::string& name);

class ChildProcess {
  public:
    // argv[0] is resolved against $PATH (or treated as a literal path if it
    // contains a '/', matching execvp's own convention) before spawning.
    // Throws std::runtime_error if argv is empty, the executable can't be
    // resolved/isn't executable, pipe creation fails, or posix_spawn itself
    // fails synchronously.
    explicit ChildProcess(const std::vector<std::string>& argv, StderrMode stderrMode = StderrMode::Discard);

    // Wraps already-open, already-connected file descriptors directly,
    // taking ownership of both -- for a caller that manages the underlying
    // connection itself (a test driving a raw pipe pair with no real
    // subprocess involved). pid, if given, is reaped/killed by the
    // destructor the same way the process-spawning constructor's child is;
    // -1 (the default) means "no process to manage," skipping that logic
    // entirely.
    ChildProcess(int readFd, int writeFd, pid_t pid = -1) noexcept;

    ~ChildProcess();

    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ChildProcess(const ChildProcess&)            = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // write-side-hang-protection follow-up. Loops over ::write() to handle
    // partial writes/EINTR, bounded by timeout: each individual ::write() is
    // preceded by a WaitWritable check, so a child that stops draining its
    // stdin (wedged, busy, or its own pipe buffer full) fails this call
    // after timeout instead of blocking the caller's thread indefinitely --
    // the write-side twin of ReadSome(timeout)/WaitReadable below. Throws
    // std::runtime_error on timeout, or on any other write error (most
    // notably EPIPE, the child having already exited and closed its stdin).
    void WriteAll(std::string_view data, std::chrono::milliseconds timeout) const;

    // One ::read() call's worth of bytes (retrying on EINTR) -- NOT
    // frame-shaped, just whatever the kernel currently has buffered. Returns
    // an empty string on EOF (the child exited and closed its end). Throws
    // std::runtime_error on a genuine read error.
    [[nodiscard]] std::string ReadSome() const;

    // subprocess-hang-protection follow-up. True if the read end has data
    // (or EOF) ready within timeout; false if it timed out with nothing
    // ready. poll()-based -- the shared primitive every byte-level framing
    // reader (Lsp/Acp/Mcp Transport) and ReadSome(timeout) below build on.
    // Throws std::runtime_error on a genuine poll() error.
    //
    // closed-connection-never-parks follow-up: false is also returned
    // immediately, without polling at all, once the connection is closed
    // (CloseConnection()/~ChildProcess having set readFd_ to -1, or a
    // moved-from instance). poll(2) *ignores* a negative fd rather than
    // failing on it -- it just reports revents == 0 for that entry -- so a
    // one-entry pollfd set whose only fd is -1 parks for the full timeout,
    // and the unbounded (negative-timeout) first-byte wait every framing
    // reader uses for an idle connection parks forever. That was a real
    // deadlock, not a theoretical one: a protocol client's read thread that
    // had not yet been scheduled into its first ReadMessage() by the time
    // its owner was destroyed entered this method against the
    // already-closed transport and never came back, so the destructor's
    // own join() on that thread never returned either (found 2026-09-08 via
    // a core dump of a wedged AcpClient test -- the flaky protocol-client
    // timeouts under `ctest -j8`). A closed connection can never become
    // readable, so reporting that immediately is both correct and what
    // makes every caller's teardown path terminate.
    [[nodiscard]] bool WaitReadable(std::chrono::milliseconds timeout) const;

    // write-side-hang-protection follow-up. WaitReadable's write-side twin:
    // true if the write end can accept data (or has an error condition
    // ready to report) within timeout; false if it timed out with nothing
    // ready -- including, per WaitReadable's own note above, the
    // immediate-false closed-connection case.
    [[nodiscard]] bool WaitWritable(std::chrono::milliseconds timeout) const;

    // Same contract as ReadSome() above, except returns std::nullopt instead
    // of blocking indefinitely when nothing arrives within timeout. Empty
    // string still means EOF; a non-empty string is real data.
    [[nodiscard]] std::optional<std::string> ReadSome(std::chrono::milliseconds timeout) const;

    // Raw fd accessors -- Transport's own byte-at-a-time frame parsing
    // (ReadLine/ReadExact) needs direct fd access rather than ReadSome's
    // "whatever's available" semantics, so it reads directly against these
    // rather than duplicating that logic here.
    [[nodiscard]] int ReadFd() const noexcept;
    [[nodiscard]] int WriteFd() const noexcept;

    // lsp-stderr-capture follow-up. The read end of the stderr pipe when
    // constructed with StderrMode::Capture; -1 otherwise (Discard/
    // MergeWithStdout, or the raw-fd constructor, which has no stderr
    // concept at all). A caller reads this directly with its own ::read()
    // loop, the same convention Transport.cpp's ReadLine/ReadExact already
    // use against ReadFd() -- no buffered-reader wrapper exists here to
    // duplicate.
    [[nodiscard]] int StderrFd() const noexcept;

    [[nodiscard]] pid_t Pid() const noexcept;

    // Blocks until the child exits and reaps it, returning its exit code --
    // or std::nullopt if it was terminated by a signal (e.g. via Kill()) or
    // there is no managed process to begin with (pid_ <= 0). Meant to be
    // called once, right after ReadSome() has observed EOF (so the child has
    // almost certainly already exited and this returns promptly) -- doing
    // the reap here, rather than leaving it to the destructor, is what lets
    // a caller (TaskProcess) learn the real exit code instead of the
    // destructor's own reap discarding it. Safe to call even if Kill() was
    // called first -- Kill() already reaps and sets pid_ to -1, so this
    // simply returns nullopt immediately in that case, same as the
    // destructor's own pid_ <= 0 check.
    std::optional<int> WaitForExit() noexcept;

    // Sends SIGKILL and blocks until the child is reaped -- for explicit,
    // user-triggered cancellation (e.g. cancel-task), as opposed to the
    // destructor's own graceful-then-forceful teardown. A no-op if there is
    // no managed process (pid_ <= 0, e.g. this ChildProcess was built via
    // the raw-fd constructor with no pid, or Kill() was already called).
    // Does not close the fds -- ReadSome()/the destructor still observe a
    // clean EOF once the kernel tears down the killed process's own fd
    // table, same as any other process exit.
    void Kill() noexcept;

    // broker-reader-deadlock follow-up. Closes this object's own fds in
    // place -- exactly what the destructor does to them (shutdown() then
    // close(), see ~ChildProcess()'s own comment on why both) -- without
    // reaping/killing the child and without destroying the object. Exists
    // for the "wake a reader thread blocked in a read on another thread"
    // idiom: destroying the whole ChildProcess out from under that reader
    // does wake it, but the reader then returns into a freed object, so the
    // owner must be able to close the connection while the reader still
    // holds a reference and let the last reference do the real teardown.
    // Idempotent; safe to call with a reader parked in a blocking read (the
    // shutdown() is what unblocks it) and safe to call before the
    // destructor, which simply finds the fds already closed and goes
    // straight to reaping the child.
    void CloseConnection() noexcept;

  private:
    int   writeFd_  = -1;
    int   readFd_   = -1;
    int   stderrFd_ = -1; // lsp-stderr-capture follow-up -- see StderrFd()
    pid_t pid_      = -1;
};

} // namespace ned::editor::process

#endif // NED_EDITOR_PROCESS_CHILDPROCESS_H
