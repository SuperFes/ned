#include "ChildProcess.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
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
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
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

    // shutdown-hang-protection follow-up. SIGKILL is unblockable, but a
    // process wedged in the same contention that motivated killing it in
    // the first place (a clangd instance thrashing shared page cache
    // against a sibling cold-indexing process, say) can still take a real
    // moment to actually get scheduled, die, and be reaped -- the plain
    // blocking waitpid(pid, &status, 0) this replaces had no bound at all,
    // so the caller's thread (the destructor, Kill(), and move-assignment
    // below all run on the main thread in every real caller) parked for
    // however long that took. Reproduced live: /proc/<pid>/task/<tid>/wchan
    // == do_wait on ned's own main thread during rapid open-quit cycles
    // against a cold, multiply-contended clangd index (see ROADMAP.md's
    // shutdown-hang entry -- the earlier guess that this was a broker
    // socket read was wrong; it's this waitpid). Bounded WNOHANG poll
    // instead, the same shape the pre-kill grace loop already uses just
    // above each call site; giving up after budgetMs leaves a zombie
    // rather than a frozen editor -- harmless (a zombie holds nothing but
    // a process-table slot) and reaped by init the moment this process
    // itself exits, if not sooner.
    bool ReapAfterKill(pid_t pid, int* status, int budgetMs = 3000) {
        for (int elapsed = 0; elapsed < budgetMs; elapsed += 10) {
            if (::waitpid(pid, status, WNOHANG) == pid) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    // How long a child gets to exit on its own after its stdin closes,
    // before the destructor escalates -- a hung/misbehaving child must
    // never hang editor shutdown.
    constexpr std::chrono::milliseconds kExitGrace{200};
    // How long a SIGKILLed child gets to be reaped -- see ReapAfterKill on
    // why this is bounded at all.
    constexpr std::chrono::milliseconds kKillReapBudget{3000};

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

    int        stdinPipe[2]  = {-1, -1};
    int        stdoutPipe[2] = {-1, -1};
    int        stderrPipe[2] = {-1, -1};
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
    OpenControlFds();
}

ChildProcess::ChildProcess(int readFd, int writeFd, pid_t pid) : writeFd_(writeFd), readFd_(readFd), pid_(pid) {
    SetNonBlocking(writeFd_);
    OpenControlFds();
}

void ChildProcess::OpenControlFds() {
    wakeFd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wakeFd_ >= 0 && pid_ > 0) {
        // Race-free: the child is ours and still unreaped. A raw syscall
        // because glibc's <sys/pidfd.h> declares pidfd_open without C
        // linkage.
        pidFd_ = static_cast<int>(::syscall(SYS_pidfd_open, pid_, 0));
    }
    if (wakeFd_ >= 0 && (pid_ <= 0 || pidFd_ >= 0)) {
        return;
    }
    const int error = errno;
    if (pid_ > 0) {
        int status = 0;
        ::kill(-pid_, SIGKILL); // whole process group -- see the spawn site's own comment
        ReapAfterKill(pid_, &status);
    }
    for (const int fd : {writeFd_, readFd_, stderrFd_, wakeFd_}) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    throw std::runtime_error(std::string("ned: ChildProcess: ") + std::strerror(error));
}

void ChildProcess::CloseConnection() noexcept {
    if (closed_.exchange(true)) {
        return;
    }
    // A socket (the raw-fd constructor's broker/bridge connections) is also
    // shut down so its peer sees the hang-up; for a pipe this is a harmless
    // ENOTSOCK. Neither frees a descriptor number.
    for (const int fd : {writeFd_, readFd_, stderrFd_}) {
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
        }
    }
    if (wakeFd_ >= 0) {
        const std::uint64_t one = 1;
        (void)!::write(wakeFd_, &one, sizeof one);
    }
}

void ChildProcess::Release() noexcept {
    CloseConnection();
    // EOF on the child's stdin is a well-behaved child's cue to exit; a
    // child blocked writing its output gets SIGPIPE.
    for (int* fd : {&writeFd_, &readFd_, &stderrFd_}) {
        if (*fd >= 0) {
            ::close(*fd);
            *fd = -1;
        }
    }
    if (pidFd_ >= 0) {
        if (!WaitChildExited(kExitGrace)) {
            SignalGroupIfUnreaped(SIGKILL);
            (void)WaitChildExited(kKillReapBudget);
        }
        ReapIfExited();
        ::close(pidFd_);
        pidFd_ = -1;
    }
    if (wakeFd_ >= 0) {
        ::close(wakeFd_);
        wakeFd_ = -1;
    }
    pid_ = -1;
    closed_.store(false);
    reaped_ = false;
    exitCode_.reset();
}

ChildProcess::~ChildProcess() {
    Release();
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : writeFd_(std::exchange(other.writeFd_, -1)), readFd_(std::exchange(other.readFd_, -1)),
      stderrFd_(std::exchange(other.stderrFd_, -1)), pid_(std::exchange(other.pid_, -1)), pidFd_(std::exchange(other.pidFd_, -1)),
      wakeFd_(std::exchange(other.wakeFd_, -1)), closed_(other.closed_.exchange(false)), reaped_(std::exchange(other.reaped_, false)),
      exitCode_(std::exchange(other.exitCode_, std::nullopt)) {
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        Release();
        writeFd_  = std::exchange(other.writeFd_, -1);
        readFd_   = std::exchange(other.readFd_, -1);
        stderrFd_ = std::exchange(other.stderrFd_, -1);
        pid_      = std::exchange(other.pid_, -1);
        pidFd_    = std::exchange(other.pidFd_, -1);
        wakeFd_   = std::exchange(other.wakeFd_, -1);
        closed_   = other.closed_.exchange(false);
        reaped_   = std::exchange(other.reaped_, false);
        exitCode_ = std::exchange(other.exitCode_, std::nullopt);
    }
    return *this;
}

void ChildProcess::WriteAll(std::string_view data, std::chrono::milliseconds timeout) const {
    std::size_t written = 0;
    while (written < data.size()) {
        if (!WaitWritable(timeout)) {
            if (closed_.load()) {
                throw std::runtime_error("ned: ChildProcess write after the connection closed");
            }
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
    return ReadSomeFrom(readFd_);
}

std::string ChildProcess::ReadSomeStderr() const {
    return ReadSomeFrom(stderrFd_);
}

std::string ChildProcess::ReadSomeFrom(int fd) const {
    char buffer[4096];
    while (true) {
        // Always polls first, so a hang-up wakes the wait. The fd may also
        // be non-blocking (it can share an open file description with a
        // WriteAll-bearing writeFd_ -- see WriteAll's own doc comment), and
        // the poll is what makes this block until data arrives regardless.
        if (!WaitFor(fd, POLLIN, std::chrono::milliseconds(-1))) {
            return {}; // a negative timeout never times out, so this is the closed-connection case -- report it as EOF
        }
        const ssize_t result = ::read(fd, buffer, sizeof(buffer));
        if (result < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (errno == EIO) {
                return {}; // a pty master's EOF: the slave side is gone
            }
            throw std::runtime_error(std::string("ned: ChildProcess read failed: ") + std::strerror(errno));
        }
        if (result == 0) {
            return {}; // EOF
        }
        return std::string(buffer, static_cast<std::size_t>(result));
    }
}

bool ChildProcess::WaitFor(int fd, short events, std::chrono::milliseconds timeout) const {
    if (fd < 0 || closed_.load()) {
        return false; // closed/moved-from -- see WaitReadable's own header doc comment on why polling a -1 fd parks instead of failing
    }
    pollfd fds[2] = {{fd, events, 0}, {wakeFd_, POLLIN, 0}};
    while (true) {
        const int result = ::poll(fds, 2, static_cast<int>(timeout.count()));
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("ned: ChildProcess poll failed: ") + std::strerror(errno));
        }
        return fds[1].revents == 0 && fds[0].revents != 0; // nothing at all == timed out
    }
}

bool ChildProcess::WaitReadable(std::chrono::milliseconds timeout) const {
    return WaitFor(readFd_, POLLIN, timeout);
}

bool ChildProcess::WaitWritable(std::chrono::milliseconds timeout) const {
    return WaitFor(writeFd_, POLLOUT, timeout);
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

bool ChildProcess::WaitChildExited(std::chrono::milliseconds timeout) const {
    if (pidFd_ < 0) {
        return true;
    }
    {
        const std::lock_guard<std::mutex> lock(reapMutex_);
        if (reaped_) {
            return true;
        }
    }
    pollfd pfd{pidFd_, POLLIN, 0}; // a pidfd turns readable when its process exits
    while (true) {
        const int result = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return result > 0;
    }
}

void ChildProcess::SignalGroupIfUnreaped(int signal) noexcept {
    const std::lock_guard<std::mutex> lock(reapMutex_);
    if (pidFd_ >= 0 && !reaped_) {
        ::kill(-pid_, signal); // whole process group -- see the spawn site's own comment
    }
}

void ChildProcess::ReapIfExited() noexcept {
    const std::lock_guard<std::mutex> lock(reapMutex_);
    if (pidFd_ < 0 || reaped_) {
        return;
    }
    siginfo_t info{};
    const int result = ::waitid(P_PIDFD, static_cast<id_t>(pidFd_), &info, WEXITED | WNOHANG);
    if (result == 0 && info.si_pid == 0) {
        return; // still running
    }
    reaped_ = true; // or ECHILD: someone else's waitpid(-1) took it, and there is nothing left to reap
    if (result == 0 && info.si_code == CLD_EXITED) {
        exitCode_ = info.si_status;
    }
}

std::optional<int> ChildProcess::WaitForExit() noexcept {
    if (pidFd_ < 0) {
        return std::nullopt;
    }
    {
        const std::lock_guard<std::mutex> lock(reapMutex_);
        if (reaped_) {
            return exitCode_;
        }
    }
    pollfd fds[2] = {{pidFd_, POLLIN, 0}, {wakeFd_, POLLIN, 0}};
    while (::poll(fds, 2, -1) < 0 && errno == EINTR) {
    }
    ReapIfExited();
    const std::lock_guard<std::mutex> lock(reapMutex_);
    return reaped_ ? exitCode_ : std::nullopt;
}

void ChildProcess::Kill() noexcept {
    SignalGroupIfUnreaped(SIGKILL);
    if (WaitChildExited(kKillReapBudget)) {
        ReapIfExited();
    }
}

std::optional<std::string> RunCapturingStdout(const std::vector<std::string>& argv) {
    if (argv.empty() || !ResolveExecutable(argv.front())) {
        return std::nullopt;
    }
    try {
        ChildProcess child(argv);
        std::string  output;
        std::string  chunk;
        while (!(chunk = child.ReadSome()).empty()) {
            output += chunk;
        }
        const std::optional<int> exitCode = child.WaitForExit();
        if (exitCode && *exitCode == 0) {
            return output;
        }
    }
    catch (const std::runtime_error&) {
        // Pipe or spawn failure: indistinguishable from "not installed" to a probe.
    }
    return std::nullopt;
}

} // namespace ned::editor::process
