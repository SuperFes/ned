#include "Ssh.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <future>
#include <string_view>
#include <system_error>
#include <thread>

#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include "Editor/Process/SelfExecutable.h"
#include "Editor/RuntimeDir.h"

namespace ned::editor::remote {

namespace {

    constexpr int         kLoginPollMs = 20;
    constexpr std::size_t kMaxStderrBytes{16 * 1024};
    // How long a stop waits for `ssh -O exit` before the master is killed.
    constexpr std::chrono::milliseconds kExitRequestWait{2000};
    // RunSession writes in chunks so a stop is noticed mid-upload.
    constexpr std::size_t kInputChunkBytes{256 * 1024};
    constexpr std::size_t kMaxSessionOutputBytes{1024 * 1024};
    constexpr int         kSessionPollMs = 50;

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
    std::vector<std::string> CommonArgv(const std::filesystem::path& controlPath, const SshOptions& options, bool batch = true) {
        return {options.program,
                "-S",
                ControlPathArgument(controlPath),
                "-o",
                batch ? "BatchMode=yes" : "BatchMode=no",
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

    // The pid in a NewControlPath name, "<pid>-<n>".
    std::optional<pid_t> ControlPathOwner(std::string_view name) {
        const auto dash = name.find('-');
        if (dash == 0 || dash == std::string_view::npos || dash + 1 == name.size()) {
            return std::nullopt;
        }
        const auto digits = [](std::string_view part) { return std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; }); };
        if (!digits(name.substr(0, dash)) || !digits(name.substr(dash + 1))) {
            return std::nullopt;
        }
        pid_t pid = 0;
        if (std::from_chars(name.data(), name.data() + dash, pid).ec != std::errc() || pid <= 0) {
            return std::nullopt;
        }
        return pid;
    }

    std::string Trimmed(std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
    }

    void AppendCapped(std::string& to, std::string_view chunk, std::size_t cap) {
        if (to.size() < cap) {
            to += chunk.substr(0, cap - to.size());
        }
    }

    constexpr std::string_view kAskpassSuffix = ".askpass";
    // How long a relay that has connected gets to finish asking.
    constexpr std::chrono::milliseconds kAskpassRequestWait{2000};

    std::filesystem::path AskpassPath(const std::filesystem::path& controlPath) {
        std::filesystem::path path = controlPath;
        path += kAskpassSuffix;
        return path;
    }

    std::string OwnExecutable() {
        const std::optional<std::filesystem::path> path = process::SelfExecutable();
        if (!path) {
            throw SshError("cannot find the editor's own executable to answer ssh");
        }
        return path->string();
    }

    // A question the handler is answering on its own thread, so the login
    // keeps draining ssh's stderr and noticing ssh give up meanwhile.
    // Destroying it withdraws the question and waits for the handler.
    struct PendingQuestion {
        PendingQuestion(AskpassRequest request, AskpassQuestion question, const AskpassHandler& handler) : request(std::move(request)) {
            std::promise<AskpassAnswer> promise;
            answer = promise.get_future();
            asking = std::jthread([handler, question = std::move(question), promise = std::move(promise)](const std::stop_token& withdrawn) mutable {
                try {
                    promise.set_value(handler(question, withdrawn));
                }
                catch (...) {
                    promise.set_exception(std::current_exception());
                }
            });
        }

        [[nodiscard]] bool Answered() const {
            return answer.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        void Reply() {
            AskpassAnswer reply;
            try {
                reply = answer.get();
            }
            catch (const std::exception&) {
                // A handler that fails cancels the question.
            }
            request.Reply(reply);
        }

        AskpassRequest             request;
        std::future<AskpassAnswer> answer;
        std::jthread               asking;
    };

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
    std::vector<std::string> argv = CommonArgv(controlPath, options, !options.askpass);
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

std::size_t SweepOrphanedMasters(const SshOptions& options) {
    std::filesystem::path dir;
    try {
        dir = RuntimeDirectory() / "ssh";
    }
    catch (const std::runtime_error&) {
        return 0;
    }
    std::error_code                    ec;
    std::vector<std::filesystem::path> orphaned;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string_view           name    = entry.path().filename().native();
        const bool                 askpass = name.ends_with(kAskpassSuffix);
        const std::optional<pid_t> owner   = ControlPathOwner(askpass ? name.substr(0, name.size() - kAskpassSuffix.size()) : name);
        if (!owner || !entry.is_socket(ec) || ::kill(*owner, 0) == 0 || errno != ESRCH) {
            continue;
        }
        if (askpass) {
            std::filesystem::remove(entry.path(), ec);
        }
        else {
            orphaned.push_back(entry.path());
        }
    }
    for (const std::filesystem::path& controlPath : orphaned) {
        try {
            // -O never logs in, so any destination does.
            process::ChildProcess request(SshExitArgv("orphaned-master", controlPath, options));
            (void)request.WaitReadable(kExitRequestWait);
        }
        catch (const std::runtime_error&) {
            // The socket goes regardless; a master still behind it ends with its link.
        }
        std::filesystem::remove(controlPath, ec);
    }
    return orphaned.size();
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
    std::optional<AskpassListener> askpass;
    process::SpawnOptions          spawn{.stderrMode = process::StderrMode::Capture, .cwd = std::nullopt, .env = {}};
    try {
        if (options_.askpass) {
            askpass.emplace(AskpassPath(controlPath_));
            spawn.env = {{"SSH_ASKPASS", options_.askpassProgram.empty() ? OwnExecutable() : options_.askpassProgram},
                         {"SSH_ASKPASS_REQUIRE", "force"},
                         {kAskpassSocketVariable, askpass->Path().string()}};
        }
        master_.emplace(SshMasterArgv(destination_, controlPath_, options_), spawn);
    }
    catch (const SshError&) {
        throw;
    }
    catch (const std::runtime_error& error) {
        throw SshError("cannot run " + options_.program + ": " + error.what());
    }

    std::optional<PendingQuestion> pending;
    const auto                     abandon = [this, &pending] {
        pending.reset();
        master_->Kill();
        master_.reset();
        std::error_code ignored;
        std::filesystem::remove(controlPath_, ignored);
    };

    // The master binds its control socket only after login succeeds.
    std::string stderrText;
    std::size_t reported = 0; // how much of stderrText earlier questions carried
    auto        deadline = std::chrono::steady_clock::now() + options_.startTimeout;
    while (!std::filesystem::is_socket(controlPath_, ec)) {
        if (stop.stop_requested()) {
            abandon();
            throw SshError("ssh to " + destination_ + " cancelled");
        }
        if (!pending && std::chrono::steady_clock::now() >= deadline) {
            abandon();
            throw SshError("ssh to " + destination_ + " didn't log in within " + std::to_string(options_.startTimeout.count()) + " ms");
        }
        std::array<pollfd, 3> fds{pollfd{.fd = master_->StderrFd(), .events = POLLIN, .revents = 0},
                                  pollfd{.fd = askpass && !pending ? askpass->Fd() : -1, .events = POLLIN, .revents = 0},
                                  pollfd{.fd = pending ? pending->request.Fd() : -1, .events = POLLIN, .revents = 0}};
        (void)::poll(fds.data(), fds.size(), kLoginPollMs);

        if (pending && (fds[2].revents != 0 || pending->Answered())) {
            // A relay that hung up was killed by ssh, done waiting on it.
            if (fds[2].revents == 0) {
                pending->Reply();
            }
            pending.reset();
            deadline = std::chrono::steady_clock::now() + options_.startTimeout;
        }
        if (fds[1].revents != 0) {
            if (std::optional<AskpassRequest> request = askpass->Accept(kAskpassRequestWait)) {
                AskpassQuestion question{.destination = destination_,
                                         .prompt      = request->Prompt(),
                                         .kind        = ClassifyAskpass(request->Hint(), request->Prompt()),
                                         .detail      = Trimmed(std::string_view(stderrText).substr(reported))};
                reported = stderrText.size();
                pending.emplace(std::move(*request), std::move(question), options_.askpass);
            }
        }
        if (fds[0].revents == 0) {
            continue;
        }
        const std::string chunk = master_->ReadSomeStderr();
        if (chunk.empty()) {
            pending.reset();
            const std::optional<int> exitCode = master_->WaitForExit();
            master_.reset();
            std::filesystem::remove(controlPath_, ec);
            throw SshError(LoginFailure(destination_, stderrText, exitCode));
        }
        AppendCapped(stderrText, chunk, kMaxStderrBytes);
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

SessionResult RunSession(const SshMaster& master, std::span<const std::string> remoteArgv, std::string_view input, std::chrono::milliseconds stallTimeout,
                         const std::stop_token& stop) {
    const std::string                    where = " on " + master.Destination();
    std::optional<process::ChildProcess> child;
    try {
        child.emplace(master.SessionArgv(remoteArgv), process::StderrMode::Capture);
    }
    catch (const std::runtime_error& error) {
        throw SshError("cannot run ssh" + where + ": " + error.what());
    }
    const auto fail = [&child](std::string message) {
        child->Kill();
        return SshError(std::move(message));
    };

    std::optional<std::string> writeFailure;
    for (std::size_t at = 0; at < input.size(); at += kInputChunkBytes) {
        if (stop.stop_requested()) {
            throw fail("cancelled" + where);
        }
        try {
            child->WriteAll(input.substr(at, kInputChunkBytes), stallTimeout);
        }
        catch (const std::runtime_error& error) {
            // Usually the command exited early; how it ended says why.
            writeFailure = error.what();
            break;
        }
    }
    child->CloseInput();

    SessionResult         result;
    std::array<pollfd, 2> fds{pollfd{.fd = child->ReadFd(), .events = POLLIN, .revents = 0},
                              pollfd{.fd = child->StderrFd(), .events = POLLIN, .revents = 0}};
    auto                  lastProgress = std::chrono::steady_clock::now();
    while (fds[0].fd >= 0 || fds[1].fd >= 0) {
        if (stop.stop_requested()) {
            throw fail("cancelled" + where);
        }
        if (std::chrono::steady_clock::now() - lastProgress > stallTimeout) {
            throw fail("no answer" + where + " within " + std::to_string(stallTimeout.count()) + " ms");
        }
        if (::poll(fds.data(), fds.size(), kSessionPollMs) <= 0) {
            continue;
        }
        lastProgress = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].fd < 0 || fds[i].revents == 0) {
                continue;
            }
            const std::string chunk = i == 0 ? child->ReadSome() : child->ReadSomeStderr();
            if (chunk.empty()) {
                fds[i].fd = -1;
            }
            else {
                AppendCapped(i == 0 ? result.output : result.errors, chunk, kMaxSessionOutputBytes);
            }
        }
    }
    result.exitCode = child->WaitForExit();
    if (writeFailure && result.exitCode == 0) {
        throw SshError("writing to ssh" + where + ": " + *writeFailure);
    }
    return result;
}

} // namespace ned::editor::remote
