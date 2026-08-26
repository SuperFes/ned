#include "ChildProcess.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

// posix_spawn's envp argument needs the process's own environment -- POSIX
// guarantees this global exists, just not in a standard header.
extern char** environ;

namespace ned::editor::process {

namespace {

    // write-side-hang-protection follow-up. A *blocking* write() to a pipe
    // does not return early with a partial count once its own free space
    // runs out -- confirmed live: it keeps waiting internally for the
    // reader to drain more, for the entire requested length, which defeats
    // a WaitWritable poll() check that only ever runs once *before* the
    // call. O_NONBLOCK is what keeps each individual ::write() call inside
    // WriteAll itself bounded; WaitWritable still does the real waiting, in
    // a loop, between short, non-blocking writes. Applied once here at
    // construction (never toggled per-call) since writeFd_ is never shared
    // with anything that wants blocking semantics -- WriteAll is its only
    // writer.
    void SetNonBlocking(int fd) {
        if (fd < 0) {
            return;
        }
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags != -1) {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

} // namespace

// Manual $PATH search -- see this file's own header comment for why this
// exists instead of just calling posix_spawnp. Hoisted out of the anonymous
// namespace (terminal-panel follow-up) so PtyProcess's pre-fork resolution
// can reuse it -- execve after forkpty needs an already-resolved path, since
// a post-fork $PATH walk isn't async-signal-safe.
std::optional<std::string> ResolveExecutable(const std::string& name) {
    if (name.find('/') != std::string::npos) {
        return (::access(name.c_str(), X_OK) == 0) ? std::optional<std::string>(name) : std::nullopt;
    }
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) {
        return std::nullopt;
    }
    const std::string_view path(pathEnv);
    std::size_t            start = 0;
    while (start <= path.size()) {
        const std::size_t      sep = path.find(':', start);
        const std::string_view dir =
            path.substr(start, sep == std::string_view::npos ? std::string_view::npos : sep - start);
        if (!dir.empty()) {
            const std::filesystem::path candidate = std::filesystem::path(dir) / name;
            if (::access(candidate.c_str(), X_OK) == 0) {
                return candidate.string();
            }
        }
        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 1;
    }
    return std::nullopt;
}

ChildProcess::ChildProcess(const std::vector<std::string>& argv, StderrMode stderrMode) {
    if (argv.empty()) {
        throw std::runtime_error("ned: ChildProcess: empty argv");
    }

    const std::optional<std::string> resolved = ResolveExecutable(argv[0]);
    if (!resolved) {
        throw std::runtime_error("ned: executable not found (checked $PATH): " + argv[0]);
    }

    int stdinPipe[2]  = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    const bool captureStderr = (stderrMode == StderrMode::Capture);
    if (::pipe(stdinPipe) != 0 || ::pipe(stdoutPipe) != 0 || (captureStderr && ::pipe(stderrPipe) != 0)) {
        throw std::runtime_error(std::string("ned: ChildProcess: pipe() failed: ") + std::strerror(errno));
    }

    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_init(&fileActions);
    posix_spawn_file_actions_adddup2(&fileActions, stdinPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&fileActions, stdinPipe[0]);
    posix_spawn_file_actions_addclose(&fileActions, stdinPipe[1]);
    posix_spawn_file_actions_adddup2(&fileActions, stdoutPipe[1], STDOUT_FILENO);
    if (stderrMode == StderrMode::MergeWithStdout) {
        // Same fd as stdout in the child, shell `2>&1`-equivalent -- the
        // build/test tool's error output interleaves into the one stream a
        // task-runner buffer streams from. Must run before stdoutPipe[1] is
        // closed below -- file actions execute in the order added, so
        // dup2'ing an already-closed fd here would fail with EBADF.
        posix_spawn_file_actions_adddup2(&fileActions, stdoutPipe[1], STDERR_FILENO);
    }
    else if (captureStderr) {
        // lsp-stderr-capture follow-up: its own separate pipe, never
        // interleaved with stdout -- stdout carries a framed protocol
        // (LSP/DAP's Content-Length messages) that a stray stderr byte would
        // corrupt.
        posix_spawn_file_actions_adddup2(&fileActions, stderrPipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&fileActions, stderrPipe[0]);
        posix_spawn_file_actions_addclose(&fileActions, stderrPipe[1]);
    }
    else {
        posix_spawn_file_actions_addopen(&fileActions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }
    posix_spawn_file_actions_addclose(&fileActions, stdoutPipe[1]);
    posix_spawn_file_actions_addclose(&fileActions, stdoutPipe[0]);

    std::vector<char*> childArgv;
    childArgv.reserve(argv.size() + 1);
    for (const std::string& arg : argv) {
        childArgv.push_back(const_cast<char*>(arg.c_str()));
    }
    childArgv.push_back(nullptr);

    // process-group-kill follow-up: puts the child in a brand-new process
    // group (pgid == its own pid) rather than inheriting ned's own group.
    // A command that's itself a wrapper -- npx, sh -c, ... -- commonly forks
    // a real grandchild without exec-replacing itself; SIGKILLing only the
    // single pid posix_spawn hands back then leaves that grandchild alive,
    // still holding the stdout pipe open, so a reader thread blocked on it
    // never sees EOF and Kill()/~ChildProcess() hangs forever waiting to
    // join. Confirmed live: `npx claude-code-acp` forks a real `node`
    // process this way. Kill()/~ChildProcess()'s kill(-pid_, ...) below
    // targets the whole group instead of just this one pid.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

    pid_t     childPid    = -1;
    const int spawnResult = posix_spawn(&childPid, resolved->c_str(), &fileActions, &attr, childArgv.data(), environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&fileActions);

    if (spawnResult != 0) {
        ::close(stdinPipe[0]);
        ::close(stdinPipe[1]);
        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        if (captureStderr) {
            ::close(stderrPipe[0]);
            ::close(stderrPipe[1]);
        }
        throw std::runtime_error(std::string("ned: posix_spawn failed for ") + *resolved + ": " + std::strerror(spawnResult));
    }

    // Parent keeps the write end of stdin and the read end of stdout; the
    // corresponding child-side ends are only needed by the child (already
    // dup2'd into place, closed there by fileActions above).
    ::close(stdinPipe[0]);
    ::close(stdoutPipe[1]);
    if (captureStderr) {
        ::close(stderrPipe[1]);
    }

    writeFd_  = stdinPipe[1];
    readFd_   = stdoutPipe[0];
    stderrFd_ = captureStderr ? stderrPipe[0] : -1;
    pid_      = childPid;
    SetNonBlocking(writeFd_);
}

ChildProcess::ChildProcess(int readFd, int writeFd, pid_t pid) noexcept : writeFd_(writeFd), readFd_(readFd), pid_(pid) {
    SetNonBlocking(writeFd_);
}

ChildProcess::~ChildProcess() {
    // shutdown() before close() on all three -- for a real spawned child's
    // pipe fds this is a harmless ENOTSOCK no-op (pipes aren't sockets), but
    // for the raw-fd constructor's socket case (LSP broker connect/accept:
    // two independently-closeable dups of one connected AF_UNIX socket, no
    // separate child process to reap) plain close() is not enough: closing
    // one dup doesn't touch the shared socket's connection state while
    // another dup keeps it alive, and even the final close() can't unblock a
    // different thread already parked in a blocking read() on it, since that
    // in-flight syscall holds its own reference until it returns -- exactly
    // the close()-vs-shutdown() gotcha LspBrokerMain.cpp's own
    // listen-socket/accept() teardown comment documents for the daemon side.
    // shutdown(SHUT_RDWR) changes the socket's protocol state directly,
    // unblocking a concurrent reader in any thread regardless of remaining
    // fd references.
    if (writeFd_ >= 0) {
        ::shutdown(writeFd_, SHUT_RDWR);
        ::close(writeFd_); // EOF on the child's stdin -- a well-behaved child treats this as a shutdown signal
    }
    if (readFd_ >= 0) {
        ::shutdown(readFd_, SHUT_RDWR);
        ::close(readFd_);
    }
    if (stderrFd_ >= 0) {
        ::shutdown(stderrFd_, SHUT_RDWR);
        ::close(stderrFd_); // lsp-stderr-capture follow-up -- unblocks a stderr reader thread's blocking read via EOF, same as readFd_ above
    }
    if (pid_ > 0) {
        int status = 0;
        // Bounded grace period for the child to exit on its own after the
        // stdin-EOF above, before escalating -- a hung/misbehaving child
        // must never hang editor shutdown.
        bool reaped = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                reaped = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!reaped) {
            ::kill(-pid_, SIGKILL); // whole process group -- see the spawn site's own comment
            ::waitpid(pid_, &status, 0);
        }
    }
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : writeFd_(std::exchange(other.writeFd_, -1)), readFd_(std::exchange(other.readFd_, -1)),
      stderrFd_(std::exchange(other.stderrFd_, -1)), pid_(std::exchange(other.pid_, -1)) {
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        // See ~ChildProcess()'s own comment on why shutdown() precedes close() here too.
        if (writeFd_ >= 0) {
            ::shutdown(writeFd_, SHUT_RDWR);
            ::close(writeFd_);
        }
        if (readFd_ >= 0) {
            ::shutdown(readFd_, SHUT_RDWR);
            ::close(readFd_);
        }
        if (stderrFd_ >= 0) {
            ::shutdown(stderrFd_, SHUT_RDWR);
            ::close(stderrFd_);
        }
        if (pid_ > 0) {
            int status = 0;
            ::kill(-pid_, SIGKILL); // whole process group -- see the spawn site's own comment
            ::waitpid(pid_, &status, 0);
        }
        writeFd_  = std::exchange(other.writeFd_, -1);
        readFd_   = std::exchange(other.readFd_, -1);
        stderrFd_ = std::exchange(other.stderrFd_, -1);
        pid_      = std::exchange(other.pid_, -1);
    }
    return *this;
}

void ChildProcess::WriteAll(std::string_view data, std::chrono::milliseconds timeout) const {
    std::size_t written = 0;
    while (written < data.size()) {
        if (!WaitWritable(timeout)) {
            throw std::runtime_error("ned: ChildProcess write stalled (child not draining stdin)");
        }
        const ssize_t result = ::write(writeFd_, data.data() + written, data.size() - written);
        if (result < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // interrupted, or a spurious/racy post-poll wakeup -- WaitWritable loops back around
            }
            throw std::runtime_error(std::string("ned: ChildProcess write failed: ") + std::strerror(errno));
        }
        written += static_cast<std::size_t>(result);
    }
}

std::string ChildProcess::ReadSome() const {
    char buffer[4096];
    while (true) {
        const ssize_t result = ::read(readFd_, buffer, sizeof(buffer));
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // write-side-hang-protection follow-up: readFd_ may now be
                // non-blocking (it can share an open file description with
                // a WriteAll-bearing writeFd_ -- see that method's own doc
                // comment), which would otherwise turn this method's
                // documented "blocks until data" contract into a spurious
                // immediate empty-handed return. WaitReadable's negative-
                // timeout sentinel restores the real blocking-until-ready
                // wait.
                [[maybe_unused]] const bool ready = WaitReadable(std::chrono::milliseconds(-1)); // always true -- a negative timeout never times out
                continue;
            }
            throw std::runtime_error(std::string("ned: ChildProcess read failed: ") + std::strerror(errno));
        }
        if (result == 0) {
            return {}; // EOF
        }
        return std::string(buffer, static_cast<std::size_t>(result));
    }
}

bool ChildProcess::WaitReadable(std::chrono::milliseconds timeout) const {
    pollfd pfd{readFd_, POLLIN, 0};
    while (true) {
        const int result = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("ned: ChildProcess poll failed: ") + std::strerror(errno));
        }
        return result > 0; // 0 == timed out, nothing ready
    }
}

bool ChildProcess::WaitWritable(std::chrono::milliseconds timeout) const {
    pollfd pfd{writeFd_, POLLOUT, 0};
    while (true) {
        const int result = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("ned: ChildProcess poll failed: ") + std::strerror(errno));
        }
        return result > 0; // 0 == timed out, nothing ready
    }
}

std::optional<std::string> ChildProcess::ReadSome(std::chrono::milliseconds timeout) const {
    if (!WaitReadable(timeout)) {
        return std::nullopt;
    }
    return ReadSome();
}

int ChildProcess::ReadFd() const noexcept {
    return readFd_;
}

int ChildProcess::WriteFd() const noexcept {
    return writeFd_;
}

int ChildProcess::StderrFd() const noexcept {
    return stderrFd_;
}

pid_t ChildProcess::Pid() const noexcept {
    return pid_;
}

std::optional<int> ChildProcess::WaitForExit() noexcept {
    if (pid_ <= 0) {
        return std::nullopt;
    }
    int status = 0;
    ::waitpid(pid_, &status, 0);
    pid_ = -1; // reaped -- destructor must not try again
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return std::nullopt; // terminated by a signal (e.g. Kill())
}

void ChildProcess::Kill() noexcept {
    if (pid_ > 0) {
        int status = 0;
        ::kill(-pid_, SIGKILL); // whole process group -- see the spawn site's own comment
        ::waitpid(pid_, &status, 0);
        pid_ = -1; // reaped -- destructor must not try again
    }
}

} // namespace ned::editor::process
