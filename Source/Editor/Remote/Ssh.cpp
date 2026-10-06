#include "Ssh.h"

#include <algorithm>
#include <atomic>
#include <string_view>
#include <system_error>

#include <poll.h>
#include <unistd.h>

#include "Editor/RuntimeDir.h"

namespace ned::editor::remote {

namespace {

    constexpr int         kLoginPollMs = 20;
    constexpr std::size_t kMaxStderrBytes{16 * 1024};
    // How long a stop waits for `ssh -O exit` before the master is killed.
    constexpr std::chrono::milliseconds kExitRequestWait{2000};

    bool IsShellSafe(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || std::string_view("_@%+=:,./-").find(c) != std::string_view::npos;
    }

    // ssh percent-expands the control path, so a literal '%' is doubled.
    std::string ControlPathArgument(const std::filesystem::path& controlPath) {
        std::string escaped;
        for (const char c : controlPath.string()) {
            escaped += c;
            if (c == '%') {
                escaped += '%';
            }
        }
        return escaped;
    }

    // Options every role shares. ClearAllForwardings keeps the master from
    // binding the forwards a user's config sets up for interactive logins.
    std::vector<std::string> CommonArgv(const std::filesystem::path& controlPath, const SshOptions& options) {
        return {options.program,
                "-S",
                ControlPathArgument(controlPath),
                "-o",
                "BatchMode=yes",
                "-o",
                "RemoteCommand=none",
                "-o",
                "RequestTTY=no",
                "-o",
                "ClearAllForwardings=yes",
                "-o",
                "ConnectTimeout=" + std::to_string(options.connectTimeout.count()),
                "-o",
                "ServerAliveInterval=" + std::to_string(options.aliveInterval.count()),
                "-o",
                "ServerAliveCountMax=" + std::to_string(options.aliveCountMax)};
    }

    std::string Trimmed(std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
    }

    std::string LoginFailure(const std::string& destination, std::string_view stderrText, std::optional<int> exitCode) {
        if (std::string message = Trimmed(stderrText); !message.empty()) {
            return message;
        }
        return "ssh to " + destination + (exitCode ? " exited with code " + std::to_string(*exitCode) : std::string(" was killed"));
    }

} // namespace

std::string QuoteRemoteWord(std::string_view word) {
    if (!word.empty() && std::ranges::all_of(word, IsShellSafe)) {
        return std::string(word);
    }
    std::string quoted = "'";
    for (const char c : word) {
        if (c == '\'') {
            quoted += "'\\''";
        }
        else {
            quoted += c;
        }
    }
    quoted += '\'';
    return quoted;
}

std::string RemoteCommandLine(std::span<const std::string> argv) {
    std::string line;
    for (const std::string& word : argv) {
        if (!line.empty()) {
            line += ' ';
        }
        line += QuoteRemoteWord(word);
    }
    return line;
}

std::vector<std::string> SshMasterArgv(const std::string& destination, const std::filesystem::path& controlPath, const SshOptions& options) {
    std::vector<std::string> argv = CommonArgv(controlPath, options);
    // ControlPersist=no keeps the master in the foreground, owned by us,
    // even under a user config that persists masters.
    argv.insert(argv.end(), {"-M", "-N", "-n", "-o", "ControlPersist=no", "--", destination});
    return argv;
}

std::vector<std::string> SshSessionArgv(const std::string& destination, const std::filesystem::path& controlPath, const SshOptions& options,
                                        std::span<const std::string> remoteArgv) {
    std::vector<std::string> argv = CommonArgv(controlPath, options);
    argv.insert(argv.end(), {"-o", "ControlMaster=no", "-T", "--", destination, RemoteCommandLine(remoteArgv)});
    return argv;
}

std::vector<std::string> SshExitArgv(const std::string& destination, const std::filesystem::path& controlPath, const SshOptions& options) {
    std::vector<std::string> argv = CommonArgv(controlPath, options);
    argv.insert(argv.end(), {"-O", "exit", "--", destination});
    return argv;
}

std::filesystem::path NewControlPath() {
    static std::atomic<unsigned> next{0};
    return EnsureRuntimeDirectory("ssh") / (std::to_string(::getpid()) + "-" + std::to_string(next++));
}

SshMaster::SshMaster(std::string destination, SshOptions options, std::filesystem::path controlPath) : destination_(std::move(destination)), options_(std::move(options)), controlPath_(controlPath.empty() ? NewControlPath() : std::move(controlPath)) {
}

SshMaster::~SshMaster() {
    Stop();
}

void SshMaster::Start(const std::stop_token& stop) {
    if (master_) {
        return;
    }
    std::error_code ec;
    // Only a dead process with a recycled pid could have left one here.
    std::filesystem::remove(controlPath_, ec);
    try {
        master_.emplace(SshMasterArgv(destination_, controlPath_, options_), process::StderrMode::Capture);
    }
    catch (const std::runtime_error& error) {
        throw SshError("cannot run " + options_.program + ": " + error.what());
    }

    const auto abandon = [this] {
        master_->Kill();
        master_.reset();
        std::error_code ignored;
        std::filesystem::remove(controlPath_, ignored);
    };

    // The master binds its control socket only after login succeeds.
    std::string stderrText;
    const auto  deadline = std::chrono::steady_clock::now() + options_.startTimeout;
    while (!std::filesystem::is_socket(controlPath_, ec)) {
        if (stop.stop_requested()) {
            abandon();
            throw SshError("ssh to " + destination_ + " cancelled");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            abandon();
            throw SshError("ssh to " + destination_ + " didn't log in within " + std::to_string(options_.startTimeout.count()) + " ms");
        }
        pollfd readable{.fd = master_->StderrFd(), .events = POLLIN, .revents = 0};
        if (::poll(&readable, 1, kLoginPollMs) <= 0) {
            continue;
        }
        const std::string chunk = master_->ReadSomeStderr();
        if (chunk.empty()) {
            const std::optional<int> exitCode = master_->WaitForExit();
            master_.reset();
            std::filesystem::remove(controlPath_, ec);
            throw SshError(LoginFailure(destination_, stderrText, exitCode));
        }
        if (stderrText.size() < kMaxStderrBytes) {
            stderrText += chunk.substr(0, kMaxStderrBytes - stderrText.size());
        }
    }
}

void SshMaster::Stop() noexcept {
    if (!master_) {
        return;
    }
    std::error_code ec;
    if (std::filesystem::is_socket(controlPath_, ec)) {
        try {
            // EOF on its stdout once the request has been answered.
            process::ChildProcess request(SshExitArgv(destination_, controlPath_, options_));
            (void)request.WaitReadable(kExitRequestWait);
        }
        catch (...) {
            // Destroying the master below kills it instead.
        }
    }
    master_.reset();
    std::filesystem::remove(controlPath_, ec);
}

std::vector<std::string> SshMaster::SessionArgv(std::span<const std::string> remoteArgv) const {
    return SshSessionArgv(destination_, controlPath_, options_, remoteArgv);
}

const std::string& SshMaster::Destination() const noexcept {
    return destination_;
}

const std::filesystem::path& SshMaster::ControlPath() const noexcept {
    return controlPath_;
}

} // namespace ned::editor::remote
