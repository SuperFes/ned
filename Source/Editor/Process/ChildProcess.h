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

#include <atomic>
#include <chrono>
#include <mutex>
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

// Threading: reads, waits, WriteAll, Kill and CloseConnection may run on
// different threads at once. Construction, moves and destruction may not --
// an owner with threads on this object calls CloseConnection, joins them,
// and only then destroys it. Descriptors therefore stay open until the
// destructor, so a thread still inside a call can never touch a descriptor
// number the process has since reused, and the child is reaped through a
// pidfd, so no wait or signal can reach a recycled pid.
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
    // entirely. pid must be this process's own unreaped child. Throws
    // std::runtime_error, after releasing the fds and killing pid, if the
    // descriptors this class needs of its own can't be created.
    ChildProcess(int readFd, int writeFd, pid_t pid = -1);

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
    // an empty string on EOF (the child exited and closed its end, or a pty
    // master's EIO, which is how a pty reports the same thing) or once the
    // connection is closed. Throws std::runtime_error on a genuine read
    // error.
    [[nodiscard]] std::string ReadSome() const;

    // ReadSome() against the StderrMode::Capture pipe; empty at once when
    // stderr isn't captured.
    [[nodiscard]] std::string ReadSomeStderr() const;

    // subprocess-hang-protection follow-up. True if the read end has data
    // (or EOF) ready within timeout; false if it timed out with nothing
    // ready. poll()-based -- the shared primitive every byte-level framing
    // reader (Lsp/Acp/Mcp Transport) and ReadSome(timeout) below build on.
    // Throws std::runtime_error on a genuine poll() error.
    //
    // closed-connection-never-parks follow-up: false is also returned
    // immediately once the connection is closed (CloseConnection(), or a
    // moved-from instance), and a wait already in progress returns false
    // the moment CloseConnection() runs. poll(2) *ignores* a negative fd rather than
    // failing on it -- it just reports revents == 0 for that entry -- so a
    // one-entry pollfd set whose only fd is -1 parks for the full timeout,
    // and the unbounded (negative-timeout) first-byte wait every framing
    // reader uses for an idle connection parks forever. That was a real
    // deadlock, not a theoretical one: a protocol client's read thread that
    // had not yet been scheduled into its first ReadMessage() by the time
    // its owner was destroyed entered this method against the
    // already-closed transport and never came back, so the destructor's
    // own join() on that thread never returned either (found 2026-09-08 via
    // a core dump of a wedged Client test -- the flaky protocol-client
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

    // The child's pid as spawned, for logging; -1 if there is none. Stays
    // the same after the child is reaped.
    [[nodiscard]] pid_t Pid() const noexcept;

    // Blocks until the child exits and reaps it, returning its exit code --
    // or std::nullopt if it was terminated by a signal (e.g. via Kill()),
    // there is no managed process, or the connection was closed first (the
    // child is then left to the destructor). Doing the reap here, rather
    // than leaving it to the destructor, is what lets a caller (TaskProcess)
    // learn the real exit code. Safe to call alongside Kill() on another
    // thread, and again after the child is reaped: the outcome is kept.
    std::optional<int> WaitForExit() noexcept;

    // Sends SIGKILL to the child's process group and waits, bounded, for it
    // to be reaped -- for explicit, user-triggered cancellation (e.g.
    // cancel-task), as opposed to the destructor's own graceful-then-
    // forceful teardown. A no-op if there is no managed process or it is
    // already reaped. Does not close the fds -- ReadSome() still observes
    // a clean EOF once the kernel tears down the killed process's own fd
    // table, same as any other process exit.
    void Kill() noexcept;

    // broker-reader-deadlock follow-up. Hangs up without destroying the
    // object: every read, wait and write in progress on another thread
    // returns as at EOF, and every later one does so at once. Sockets are
    // also shut down, so a peer sees the hang-up. The descriptors stay open
    // and the child is left alone until the destructor -- see the class
    // comment. Idempotent.
    void CloseConnection() noexcept;

  private:
    // The wake eventfd and, given a pid, its pidfd; on failure releases
    // everything this object holds, kills the child, and throws.
    void OpenControlFds();

    [[nodiscard]] std::string ReadSomeFrom(int fd) const;
    // Polls fd (and the hang-up signal) for events; false on timeout or
    // once the connection is closed.
    [[nodiscard]] bool WaitFor(int fd, short events, std::chrono::milliseconds timeout) const;
    // True once the child has exited (reaped or not) within timeout.
    [[nodiscard]] bool WaitChildExited(std::chrono::milliseconds timeout) const;
    void               SignalGroupIfUnreaped(int signal) noexcept;
    void               ReapIfExited() noexcept;
    // The destructor's work, shared with move assignment.
    void Release() noexcept;

    int   writeFd_  = -1;
    int   readFd_   = -1;
    int   stderrFd_ = -1; // lsp-stderr-capture follow-up -- see StderrFd()
    pid_t pid_      = -1;
    int   pidFd_    = -1;
    // An eventfd made readable by CloseConnection, polled alongside every
    // wait so the hang-up wakes it.
    int               wakeFd_ = -1;
    std::atomic<bool> closed_ = false;

    // Guards the reap: the child is reaped once, under this lock, and is
    // signalled only under it while still unreaped -- an unreaped child's
    // pid and process group can't be reused.
    mutable std::mutex reapMutex_;
    bool               reaped_ = false;
    std::optional<int> exitCode_;
};

// Runs argv to completion, blocking, and returns its stdout iff it exits 0;
// std::nullopt on any failure (not installed, spawn error, non-zero exit).
// For short startup probes only -- there is no timeout.
[[nodiscard]] std::optional<std::string> RunCapturingStdout(const std::vector<std::string>& argv);

} // namespace ned::editor::process

#endif // NED_EDITOR_PROCESS_CHILDPROCESS_H
