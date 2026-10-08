#include "Pty.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <pty.h>
#include <unistd.h>

namespace ned::editor::process {

ChildProcess SpawnOnPty(const std::vector<std::string>& argv, TerminalSize size, const SpawnOptions& options) {
    if (argv.empty()) {
        throw std::runtime_error("ned: SpawnOnPty: empty argv");
    }
    const std::optional<std::string> path = ResolveExecutable(argv.front());
    if (!path) {
        throw std::runtime_error("ned: executable not found (checked $PATH): " + argv.front());
    }
    std::error_code error;
    if (options.cwd && !std::filesystem::is_directory(*options.cwd, error)) {
        throw std::runtime_error("ned: SpawnOnPty: not a directory: " + options.cwd->string());
    }

    // Everything execve needs is built before forking: the editor has
    // threads running, so the child may not allocate.
    std::vector<std::string> argvStorage = argv;
    std::vector<char*>       childArgv;
    for (std::string& arg : argvStorage) {
        childArgv.push_back(arg.data());
    }
    childArgv.push_back(nullptr);
    std::vector<std::string> envStorage = EnvironmentWith(options.env);
    std::vector<char*>       childEnv;
    for (std::string& entry : envStorage) {
        childEnv.push_back(entry.data());
    }
    childEnv.push_back(nullptr);
    const std::string cwd = options.cwd ? options.cwd->string() : std::string();

    winsize windowSize{};
    windowSize.ws_row = static_cast<unsigned short>(std::max(1, size.rows));
    windowSize.ws_col = static_cast<unsigned short>(std::max(1, size.cols));

    int         master = -1;
    const pid_t pid    = ::forkpty(&master, nullptr, nullptr, &windowSize);
    if (pid < 0) {
        throw std::runtime_error("ned: SpawnOnPty: forkpty failed");
    }
    if (pid == 0) {
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) {
            ::_exit(127);
        }
        ::execve(path->c_str(), childArgv.data(), childEnv.data());
        ::_exit(127);
    }

    // Close-on-exec, so no child spawned later holds the master open and
    // keeps this session from hanging up. The two halves are closed
    // independently, so they can't be one fd.
    ::fcntl(master, F_SETFD, FD_CLOEXEC);
    const int readFd = ::fcntl(master, F_DUPFD_CLOEXEC, 0);
    if (readFd < 0) {
        ::close(master);
        throw std::runtime_error("ned: SpawnOnPty: dup of pty master failed");
    }
    return ChildProcess(readFd, master, pid);
}

} // namespace ned::editor::process
