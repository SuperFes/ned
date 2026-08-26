#include "PtyProcess.h"

#include <cerrno>
#include <stdexcept>
#include <utility>

#include <pty.h>
#include <sys/ioctl.h>
#include <unistd.h>

// See ChildProcess.cpp's own use -- POSIX guarantees this global exists,
// just not in a standard header.
extern char** environ;

namespace ned::editor::terminal {

namespace {

    // Everything execve needs, fully materialized before forkpty so the
    // child side does only async-signal-safe work (fork in a process with
    // running threads -- LSP/DAP/task readers -- leaves the child's heap
    // locks in an undefined state, so no allocation may happen there).
    struct PreparedExec {
        std::string              path;
        std::vector<std::string> argvStorage;
        std::vector<char*>       argv;
        std::vector<std::string> envStorage;
        std::vector<char*>       envp;
    };

    PreparedExec PrepareExec(std::vector<std::string> argv) {
        if (argv.empty()) {
            throw std::runtime_error("ned: PtyProcess: empty argv");
        }
        const std::optional<std::string> resolved = process::ResolveExecutable(argv.front());
        if (!resolved) {
            throw std::runtime_error("ned: PtyProcess: cannot resolve executable: " + argv.front());
        }

        PreparedExec prepared;
        prepared.path        = *resolved;
        prepared.argvStorage = std::move(argv);
        for (std::string& arg : prepared.argvStorage) {
            prepared.argv.push_back(arg.data());
        }
        prepared.argv.push_back(nullptr);

        for (char** entry = environ; *entry != nullptr; ++entry) {
            const std::string_view var(*entry);
            if (var.starts_with("TERM=")) {
                continue;
            }
            prepared.envStorage.emplace_back(var);
        }
        prepared.envStorage.emplace_back("TERM=xterm-256color");
        for (std::string& var : prepared.envStorage) {
            prepared.envp.push_back(var.data());
        }
        prepared.envp.push_back(nullptr);
        return prepared;
    }

    process::ChildProcess SpawnPty(std::vector<std::string> argv, int rows, int cols) {
        const PreparedExec prepared = PrepareExec(std::move(argv));

        winsize windowSize{};
        windowSize.ws_row = static_cast<unsigned short>(std::max(1, rows));
        windowSize.ws_col = static_cast<unsigned short>(std::max(1, cols));

        int         master = -1;
        const pid_t pid    = ::forkpty(&master, nullptr, nullptr, &windowSize);
        if (pid < 0) {
            throw std::runtime_error("ned: PtyProcess: forkpty failed");
        }
        if (pid == 0) {
            // Child: session leader with the pty slave as controlling tty,
            // stdio already wired -- forkpty did all of it. Only
            // async-signal-safe calls from here.
            ::execve(prepared.path.c_str(), prepared.argv.data(), prepared.envp.data());
            ::_exit(127);
        }

        // Both ChildProcess fds must be closeable independently, so the read
        // half is a dup of the master rather than the same fd twice.
        const int readFd = ::dup(master);
        if (readFd < 0) {
            ::close(master);
            throw std::runtime_error("ned: PtyProcess: dup of pty master failed");
        }
        return process::ChildProcess(readFd, master, pid);
    }

} // namespace

PtyProcess::PtyProcess(std::vector<std::string> argv, int rows, int cols, ned::ui::EventLoop& eventLoop,
                       std::function<void(std::string_view)> onOutput, std::function<void(std::optional<int>)> onExit) : child_(SpawnPty(std::move(argv), rows, cols)), eventLoop_(eventLoop), onOutput_(std::move(onOutput)), onExit_(std::move(onExit)) {
    StartReadLoop();
}

PtyProcess::~PtyProcess() {
    // Runs before the member destructors that do the real teardown -- see
    // the header comment for why queued Posts must be defused first.
    *alive_ = false;
}

void PtyProcess::StartReadLoop() {
    // Mirrors TaskProcess::StartReadLoop -- see that function's comment for
    // why the thread starts in the constructor body. The read itself is raw
    // ::read rather than ChildProcess::ReadSome because a pty master's
    // routine end-of-life signal is EIO (child exited, slave side gone), not
    // the 0-byte EOF a pipe reports -- ReadSome would throw on it. Every
    // posted lambda carries the alive_ flag -- see ~PtyProcess.
    readThread_ = std::jthread([this](std::stop_token) {
        while (true) {
            char          buffer[4096];
            const ssize_t count = ::read(child_.ReadFd(), buffer, sizeof buffer);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // write-side-hang-protection follow-up: the master fd is
                // dup()'d between child_'s read and write halves (see
                // SpawnPty below), so WriteAll's own O_NONBLOCK (needed to
                // keep a stalled shell's write from hanging the main
                // thread) makes this read end non-blocking too. Wait for
                // real readability before retrying, exactly mirroring the
                // blocking read this loop was written to expect -- a
                // negative timeout is WaitReadable's own "block forever"
                // sentinel (poll(2)'s convention), matching idle-shell
                // silence being perfectly normal here.
                [[maybe_unused]] const bool ready = child_.WaitReadable(std::chrono::milliseconds(-1)); // always true -- a negative timeout never times out
                continue;
            }
            if (count <= 0) {
                break; // EOF, EIO (the pty flavor of EOF), or shutdown fd teardown
            }
            eventLoop_.Post([this, alive = alive_, chunk = std::string(buffer, static_cast<std::size_t>(count))] {
                if (*alive) {
                    DispatchOutput(chunk);
                }
            });
        }
        const std::optional<int> exitCode = child_.WaitForExit();
        eventLoop_.Post([this, alive = alive_, exitCode] {
            if (*alive) {
                DispatchExit(exitCode);
            }
        });
    });
}

void PtyProcess::Write(std::string_view data, std::chrono::milliseconds timeout) const {
    child_.WriteAll(data, timeout);
}

void PtyProcess::Resize(int rows, int cols) const noexcept {
    winsize windowSize{};
    windowSize.ws_row = static_cast<unsigned short>(std::max(1, rows));
    windowSize.ws_col = static_cast<unsigned short>(std::max(1, cols));
    ::ioctl(child_.WriteFd(), TIOCSWINSZ, &windowSize);
}

void PtyProcess::DispatchOutput(std::string_view chunk) {
    if (onOutput_) {
        onOutput_(chunk);
    }
}

void PtyProcess::DispatchExit(std::optional<int> exitCode) {
    if (onExit_) {
        onExit_(exitCode);
    }
}

int PtyProcess::MasterFdForTesting() const noexcept {
    return child_.WriteFd();
}

} // namespace ned::editor::terminal
