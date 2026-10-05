#include "PtyProcess.h"

#include <algorithm>
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

    PreparedExec PrepareExec(std::vector<std::string> argv, const std::vector<std::pair<std::string, std::string>>& env) {
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

        auto overridden = [&env](std::string_view var) {
            return std::any_of(env.begin(), env.end(), [var](const auto& entry) {
                return var.size() > entry.first.size() && var.starts_with(entry.first) && var[entry.first.size()] == '=';
            });
        };
        for (char** entry = environ; *entry != nullptr; ++entry) {
            const std::string_view var(*entry);
            if (var.starts_with("TERM=") || overridden(var)) {
                continue;
            }
            prepared.envStorage.emplace_back(var);
        }
        prepared.envStorage.emplace_back("TERM=xterm-256color");
        for (const auto& [name, value] : env) {
            if (name != "TERM") {
                prepared.envStorage.push_back(name + "=" + value);
            }
        }
        for (std::string& var : prepared.envStorage) {
            prepared.envp.push_back(var.data());
        }
        prepared.envp.push_back(nullptr);
        return prepared;
    }

    process::ChildProcess SpawnPty(std::vector<std::string> argv, int rows, int cols, const std::vector<std::pair<std::string, std::string>>& env) {
        const PreparedExec prepared = PrepareExec(std::move(argv), env);

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
                       std::function<void(std::string_view)> onOutput, std::function<void(std::optional<int>)> onExit,
                       const std::vector<std::pair<std::string, std::string>>& env) : stream_(SpawnPty(std::move(argv), rows, cols, env), eventLoop, std::move(onOutput), std::move(onExit)) {
}

void PtyProcess::Write(std::string_view data, std::chrono::milliseconds timeout) const {
    stream_.Child().WriteAll(data, timeout);
}

void PtyProcess::Resize(int rows, int cols) const noexcept {
    winsize windowSize{};
    windowSize.ws_row = static_cast<unsigned short>(std::max(1, rows));
    windowSize.ws_col = static_cast<unsigned short>(std::max(1, cols));
    ::ioctl(stream_.Child().WriteFd(), TIOCSWINSZ, &windowSize);
}

void PtyProcess::DispatchOutput(std::string_view chunk) {
    stream_.DispatchOutput(chunk);
}

void PtyProcess::DispatchExit(std::optional<int> exitCode) {
    stream_.DispatchExit(exitCode);
}

int PtyProcess::MasterFdForTesting() const noexcept {
    return stream_.Child().WriteFd();
}

} // namespace ned::editor::terminal
