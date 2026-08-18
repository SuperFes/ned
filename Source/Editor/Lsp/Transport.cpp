#include "Transport.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

// posix_spawn's envp argument needs the process's own environment -- POSIX
// guarantees this global exists, just not in a standard header.
extern char** environ;

namespace ned::editor::lsp {

namespace {

    // Manual $PATH search -- see Transport.h's own header comment for why
    // this exists instead of just calling posix_spawnp.
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

    // Loops over ::write() to handle partial writes and EINTR -- a single
    // write() call is not guaranteed to consume the whole buffer even for a
    // blocking fd. Throws on any other error (most notably EPIPE, the child
    // having already exited and closed its stdin).
    void WriteAll(int fd, std::string_view data) {
        std::size_t written = 0;
        while (written < data.size()) {
            const ssize_t result = ::write(fd, data.data() + written, data.size() - written);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("ned: LSP transport write failed: ") + std::strerror(errno));
            }
            written += static_cast<std::size_t>(result);
        }
    }

    // Reads exactly one line (up to and excluding a trailing "\r\n" or "\n"),
    // byte at a time -- headers are two short lines per frame, so the extra
    // syscalls are negligible; a buffered reader would have to be careful
    // never to over-read past the header/body boundary, which byte-at-a-time
    // reading sidesteps for free. Returns false on EOF before any byte of a
    // new line was read (a clean "the server exited" signal); throws on a
    // genuine read error.
    bool ReadLine(int fd, std::string& line) {
        line.clear();
        while (true) {
            char          ch     = 0;
            const ssize_t result = ::read(fd, &ch, 1);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("ned: LSP transport read failed: ") + std::strerror(errno));
            }
            if (result == 0) {
                return !line.empty() ? throw std::runtime_error("ned: LSP transport EOF mid-header-line") : false;
            }
            if (ch == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return true;
            }
            line += ch;
        }
    }

    // Reads exactly n bytes into buffer, looping over ::read() for partial
    // reads/EINTR. Returns false on EOF before n bytes were read.
    bool ReadExact(int fd, char* buffer, std::size_t n) {
        std::size_t got = 0;
        while (got < n) {
            const ssize_t result = ::read(fd, buffer + got, n - got);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("ned: LSP transport read failed: ") + std::strerror(errno));
            }
            if (result == 0) {
                return false;
            }
            got += static_cast<std::size_t>(result);
        }
        return true;
    }

} // namespace

Transport::Transport(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        throw std::runtime_error("ned: Transport: empty argv");
    }

    const std::optional<std::string> resolved = ResolveExecutable(argv[0]);
    if (!resolved) {
        throw std::runtime_error("ned: LSP server executable not found (checked $PATH): " + argv[0]);
    }

    int stdinPipe[2]  = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    if (::pipe(stdinPipe) != 0 || ::pipe(stdoutPipe) != 0) {
        throw std::runtime_error(std::string("ned: Transport: pipe() failed: ") + std::strerror(errno));
    }

    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_init(&fileActions);
    posix_spawn_file_actions_adddup2(&fileActions, stdinPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&fileActions, stdinPipe[0]);
    posix_spawn_file_actions_addclose(&fileActions, stdinPipe[1]);
    posix_spawn_file_actions_adddup2(&fileActions, stdoutPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fileActions, stdoutPipe[1]);
    posix_spawn_file_actions_addclose(&fileActions, stdoutPipe[0]);
    // Stderr capture (a real "show me the server's log" story) is a
    // documented follow-up, not silently dropped forever -- see this
    // subsystem's own ROADMAP.md entry.
    posix_spawn_file_actions_addopen(&fileActions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    std::vector<char*> childArgv;
    childArgv.reserve(argv.size() + 1);
    for (const std::string& arg : argv) {
        childArgv.push_back(const_cast<char*>(arg.c_str()));
    }
    childArgv.push_back(nullptr);

    pid_t     childPid    = -1;
    const int spawnResult = posix_spawn(&childPid, resolved->c_str(), &fileActions, nullptr, childArgv.data(), environ);
    posix_spawn_file_actions_destroy(&fileActions);

    if (spawnResult != 0) {
        ::close(stdinPipe[0]);
        ::close(stdinPipe[1]);
        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        throw std::runtime_error(std::string("ned: posix_spawn failed for ") + *resolved + ": " + std::strerror(spawnResult));
    }

    // Parent keeps the write end of stdin and the read end of stdout; the
    // corresponding child-side ends are only needed by the child (already
    // dup2'd into place, closed there by fileActions above).
    ::close(stdinPipe[0]);
    ::close(stdoutPipe[1]);

    writeFd_ = stdinPipe[1];
    readFd_  = stdoutPipe[0];
    pid_     = childPid;
}

Transport::Transport(int readFd, int writeFd, pid_t pid) noexcept : writeFd_(writeFd), readFd_(readFd), pid_(pid) {
}

Transport::~Transport() {
    if (writeFd_ >= 0) {
        ::close(writeFd_); // EOF on the child's stdin -- a well-behaved server treats this as a shutdown signal
    }
    if (readFd_ >= 0) {
        ::close(readFd_);
    }
    if (pid_ > 0) {
        int status = 0;
        // Bounded grace period for the server to exit on its own after the
        // stdin-EOF above, before escalating -- a hung/misbehaving language
        // server must never hang editor shutdown.
        bool reaped = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                reaped = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!reaped) {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, &status, 0);
        }
    }
}

Transport::Transport(Transport&& other) noexcept
    : writeFd_(std::exchange(other.writeFd_, -1)), readFd_(std::exchange(other.readFd_, -1)), pid_(std::exchange(other.pid_, -1)) {
}

Transport& Transport::operator=(Transport&& other) noexcept {
    if (this != &other) {
        if (writeFd_ >= 0) {
            ::close(writeFd_);
        }
        if (readFd_ >= 0) {
            ::close(readFd_);
        }
        if (pid_ > 0) {
            int status = 0;
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, &status, 0);
        }
        writeFd_ = std::exchange(other.writeFd_, -1);
        readFd_  = std::exchange(other.readFd_, -1);
        pid_     = std::exchange(other.pid_, -1);
    }
    return *this;
}

void Transport::WriteFrame(std::string_view jsonPayload) const {
    const std::string header = "Content-Length: " + std::to_string(jsonPayload.size()) + "\r\n\r\n";
    WriteAll(writeFd_, header);
    WriteAll(writeFd_, jsonPayload);
}

std::optional<std::string> Transport::ReadFrame() const {
    std::size_t contentLength    = 0;
    bool        sawContentLength = false;

    while (true) {
        std::string line;
        if (!ReadLine(readFd_, line)) {
            return std::nullopt; // EOF before any header -- clean "server exited" between frames
        }
        if (line.empty()) {
            break; // blank line terminates the header block
        }
        constexpr std::string_view kPrefix = "Content-Length: ";
        if (line.rfind(kPrefix, 0) == 0) {
            try {
                contentLength    = std::stoul(line.substr(kPrefix.size()));
                sawContentLength = true;
            }
            catch (const std::exception&) {
                throw std::runtime_error("ned: LSP frame has an unparseable Content-Length header: " + line);
            }
        }
        // Any other header (e.g. Content-Type) is ignored, per the LSP spec.
    }

    if (!sawContentLength) {
        throw std::runtime_error("ned: LSP frame missing Content-Length header");
    }

    std::string body(contentLength, '\0');
    if (!ReadExact(readFd_, body.data(), contentLength)) {
        return std::nullopt; // EOF mid-body -- server exited mid-message
    }
    return body;
}

pid_t Transport::Pid() const noexcept {
    return pid_;
}

} // namespace ned::editor::lsp
