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

// Discard (the LSP default -- server logs go nowhere, a documented follow-up
// of the original Transport, not a task-runner concern) or MergeWithStdout
// (dup2'd onto the same fd as stdout, i.e. shell `2>&1` -- what a task
// runner wants, since a build/test tool's error output belongs in the same
// stream as its normal output).
enum class StderrMode { Discard,
                        MergeWithStdout };

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

    // Loops over ::write() to handle partial writes/EINTR. Throws
    // std::runtime_error on any other error (most notably EPIPE, the child
    // having already exited and closed its stdin).
    void WriteAll(std::string_view data) const;

    // One ::read() call's worth of bytes (retrying on EINTR) -- NOT
    // frame-shaped, just whatever the kernel currently has buffered. Returns
    // an empty string on EOF (the child exited and closed its end). Throws
    // std::runtime_error on a genuine read error.
    [[nodiscard]] std::string ReadSome() const;

    // subprocess-hang-protection follow-up. True if the read end has data
    // (or EOF) ready within timeout; false if it timed out with nothing
    // ready. poll()-based -- the shared primitive every byte-level framing
    // reader (Lsp/Acp Transport) and ReadSome(timeout) below build on. Throws
    // std::runtime_error on a genuine poll() error.
    [[nodiscard]] bool WaitReadable(std::chrono::milliseconds timeout) const;

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

  private:
    int   writeFd_ = -1;
    int   readFd_  = -1;
    pid_t pid_     = -1;
};

} // namespace ned::editor::process

#endif // NED_EDITOR_PROCESS_CHILDPROCESS_H
