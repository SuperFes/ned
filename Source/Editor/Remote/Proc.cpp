#include "Proc.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Editor/DiagnosticsLog.h"
#include "Editor/Host/ProcCodec.h"
#include "Editor/ProcessTimeouts.h"

namespace ned::editor::remote {

using Json = nlohmann::json;

namespace {

    // The most one proc/write carries.
    constexpr std::size_t kStdinChunkBytes = 64 * 1024;

    std::atomic<std::uint64_t> g_nextHandle{0};

    void Notify(int eventFd) {
        const std::uint64_t one = 1;
        (void)!::write(eventFd, &one, sizeof one);
    }

    void Drain(int eventFd) {
        std::uint64_t count = 0;
        (void)!::read(eventFd, &count, sizeof count);
    }

    // What the relay thread, the main thread's request handlers and the
    // child's control share. Descriptors are fixed once made; the rest is
    // guarded by mutex.
    struct Relay {
        Relay(Connection& connectionIn, std::uint64_t handleIn, int ioIn, int errIn, process::StderrMode stderrModeIn) : postQueue(connectionIn.Queue()), alive(connectionIn.Token()), connection(&connectionIn), handle(handleIn), io(ioIn), err(errIn),
                                                                                                                         stderrMode(stderrModeIn), exitFd(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)), wakeFd(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
            if (exitFd < 0 || wakeFd < 0) {
                const int error = errno;
                CloseAll();
                throw std::runtime_error(std::string("ned: a relay's eventfd: ") + std::strerror(error));
            }
        }

        ~Relay() {
            CloseAll();
        }

        Relay(const Relay&)            = delete;
        Relay& operator=(const Relay&) = delete;

        void CloseAll() noexcept {
            for (const int fd : {io, err, exitFd, wakeFd}) {
                if (fd >= 0) {
                    ::close(fd);
                }
            }
        }

        // Runs fn on the main thread while the connection lasts.
        template <typename Fn>
        void OnMain(Fn fn) {
            postQueue.Post([alive = alive, fn = std::move(fn)]() mutable {
                if (alive.Alive()) {
                    fn();
                }
            });
        }

        // Main thread only.
        void Grant(std::size_t chunks) {
            if (chunks != 0 && alive.Alive()) {
                connection->Endpoint().Grant(request, chunks);
            }
        }

        // Main thread only: the spawn's answer.
        void Finish(std::optional<int> status) {
            {
                const std::lock_guard lock(mutex);
                finished = true;
                exit     = status;
            }
            Notify(exitFd);
            Notify(wakeFd);
        }

        PostQueue&                postQueue;
        LifetimeToken             alive;
        Connection*               connection; // main thread, while alive
        const std::uint64_t       handle;
        std::uint64_t             request = 0; // main thread
        const int                 io;          // our end of stdin and stdout
        const int                 err;         // our end of stderr's, or -1
        const process::StderrMode stderrMode;
        const int                 exitFd;
        const int                 wakeFd;

        std::mutex                             mutex;
        std::array<std::deque<std::string>, 3> out;    // by fd, waiting for the child's reader
        std::array<std::size_t, 3>             sent{}; // of each front, already handed on
        bool                                   finished = false;
        std::optional<int>                     exit;
        bool                                   released      = false;
        bool                                   discarding    = false; // the child's reader has gone
        bool                                   writeInFlight = false;
        bool                                   inputDone     = false;
    };

    // Hands what's queued for fd to the child's reader; returns how many
    // chunks went wholly, for credit.
    std::size_t Deliver(Relay& relay, int fd, int socket) {
        std::size_t      delivered = 0;
        std::unique_lock lock(relay.mutex);
        auto&            queue = relay.out[static_cast<std::size_t>(fd)];
        auto&            sent  = relay.sent[static_cast<std::size_t>(fd)];
        while (!queue.empty()) {
            const std::string& front  = queue.front();
            const ssize_t      result = ::send(socket, front.data() + sent, front.size() - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                // The reader is gone: what it would have read goes nowhere.
                relay.discarding = true;
                for (auto& other : relay.out) {
                    delivered += other.size();
                    other.clear();
                }
                relay.sent = {};
                break;
            }
            sent += static_cast<std::size_t>(result);
            if (sent == front.size()) {
                queue.pop_front();
                sent = 0;
                ++delivered;
            }
        }
        return delivered;
    }

    void RunRelay(const std::shared_ptr<Relay>& shared) {
        Relay& relay = *shared;
        while (true) {
            bool wantInput  = false;
            bool wantOutput = false;
            bool wantError  = false;
            {
                const std::lock_guard lock(relay.mutex);
                if (relay.released) {
                    return;
                }
                if (relay.finished && relay.out[1].empty() && relay.out[2].empty()) {
                    // As a child's exit closes its end: the reader sees EOF,
                    // and a writer EPIPE.
                    ::shutdown(relay.io, SHUT_RDWR);
                    if (relay.err >= 0) {
                        ::shutdown(relay.err, SHUT_RDWR);
                    }
                    return;
                }
                wantInput  = !relay.finished && !relay.writeInFlight && !relay.inputDone;
                wantOutput = !relay.out[1].empty();
                wantError  = !relay.out[2].empty();
            }

            std::array<pollfd, 3> fds{};
            nfds_t                count = 0;
            fds[count++]                = pollfd{relay.wakeFd, POLLIN, 0};
            const nfds_t ioIndex        = count;
            if (wantInput || wantOutput) {
                fds[count++] = pollfd{relay.io, static_cast<short>((wantInput ? POLLIN : 0) | (wantOutput ? POLLOUT : 0)), 0};
            }
            const nfds_t errIndex = count;
            if (wantError) {
                fds[count++] = pollfd{relay.err, POLLOUT, 0};
            }
            if (::poll(fds.data(), count, -1) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                LogMessage(LogCategory::General, LogSeverity::Error, std::string("ned: a remote process's relay: ") + std::strerror(errno));
                return;
            }
            if (fds[0].revents != 0) {
                Drain(relay.wakeFd);
            }

            std::size_t delivered = 0;
            if (ioIndex < count && fds[ioIndex].revents != 0) {
                const short events = fds[ioIndex].revents;
                if (wantInput && (events & (POLLIN | POLLHUP | POLLERR)) != 0) {
                    std::string   data(kStdinChunkBytes, '\0');
                    const ssize_t result = ::recv(relay.io, data.data(), data.size(), MSG_DONTWAIT);
                    if (result > 0) {
                        data.resize(static_cast<std::size_t>(result));
                        {
                            const std::lock_guard lock(relay.mutex);
                            relay.writeInFlight = true;
                        }
                        relay.OnMain([shared, data = std::move(data)]() mutable {
                            shared->connection->Endpoint().Request(
                                std::string(host::kProcWrite), host::ToJson(host::ProcWriteRequest{.handle = shared->handle, .data = std::move(data)}),
                                protocol::ResponseHandlers{.onResult =
                                                               [shared](const Json&) {
                                                                   {
                                                                       const std::lock_guard lock(shared->mutex);
                                                                       shared->writeInFlight = false;
                                                                   }
                                                                   Notify(shared->wakeFd);
                                                               },
                                                           .onError =
                                                               [shared](const protocol::RemoteError&) {
                                                                   // The child's stdin is gone: so is ours.
                                                                   {
                                                                       const std::lock_guard lock(shared->mutex);
                                                                       shared->writeInFlight = false;
                                                                       shared->inputDone     = true;
                                                                   }
                                                                   ::shutdown(shared->io, SHUT_RD);
                                                                   Notify(shared->wakeFd);
                                                               },
                                                           .onChunk      = {},
                                                           .grantsByHand = false},
                                ProtocolRequestTimeoutMs());
                        });
                    }
                    else if (result == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                        {
                            const std::lock_guard lock(relay.mutex);
                            relay.inputDone = true;
                        }
                        relay.OnMain([shared] {
                            shared->connection->Endpoint().Request(std::string(host::kProcCloseStdin), host::ProcHandleParams(shared->handle),
                                                                   protocol::ResponseHandlers{}, ProtocolRequestTimeoutMs());
                        });
                    }
                }
                if (wantOutput && (events & (POLLOUT | POLLHUP | POLLERR)) != 0) {
                    delivered += Deliver(relay, 1, relay.io);
                }
            }
            if (errIndex < count && fds[errIndex].revents != 0) {
                delivered += Deliver(relay, 2, relay.err);
            }
            if (delivered != 0) {
                relay.OnMain([shared, delivered] { shared->Grant(delivered); });
            }
        }
    }

    class RelayControl final : public process::ProcessControl {
      public:
        explicit RelayControl(std::shared_ptr<Relay> relay) : relay_(std::move(relay)), thread_([relay = relay_] { RunRelay(relay); }) {
        }

        ~RelayControl() override {
            Release();
        }

        RelayControl(const RelayControl&)            = delete;
        RelayControl& operator=(const RelayControl&) = delete;

        [[nodiscard]] int ExitFd() const noexcept override {
            return relay_->exitFd;
        }

        [[nodiscard]] std::optional<int> ExitCode() const noexcept override {
            const std::lock_guard lock(relay_->mutex);
            return relay_->exit;
        }

        void Signal(int signal) noexcept override {
            relay_->OnMain([relay = relay_, signal] {
                if (relay->finished) {
                    return;
                }
                try {
                    relay->connection->Endpoint().Request(std::string(host::kProcSignal),
                                                          host::ToJson(host::ProcSignalRequest{.handle = relay->handle, .signal = signal}),
                                                          protocol::ResponseHandlers{}, ProtocolRequestTimeoutMs());
                }
                catch (const std::invalid_argument&) {
                    LogMessage(LogCategory::General, LogSeverity::Warning, "ned: signal " + std::to_string(signal) + " can't be sent to a host");
                }
            });
        }

        void Release() noexcept override {
            if (!thread_.joinable()) {
                return;
            }
            std::size_t held = 0;
            {
                const std::lock_guard lock(relay_->mutex);
                relay_->released = true;
                for (auto& queue : relay_->out) {
                    held += queue.size();
                    queue.clear();
                }
            }
            Notify(relay_->wakeFd);
            thread_.join();
            relay_->OnMain([relay = relay_, held] {
                relay->Grant(held);
                if (!relay->finished) {
                    relay->connection->Endpoint().Request(std::string(host::kProcRelease), host::ProcHandleParams(relay->handle),
                                                          protocol::ResponseHandlers{}, ProtocolRequestTimeoutMs());
                }
            });
        }

        void ResizeTerminal(process::TerminalSize size) noexcept override {
            relay_->OnMain([relay = relay_, size] {
                if (!relay->finished) {
                    relay->connection->Endpoint().Request(std::string(host::kProcResize), host::ToJson(host::ProcResizeRequest{.handle = relay->handle, .size = size}),
                                                          protocol::ResponseHandlers{}, ProtocolRequestTimeoutMs());
                }
            });
        }

      private:
        std::shared_ptr<Relay> relay_;
        std::jthread           thread_;
    };

    std::array<int, 2> SocketPair() {
        std::array<int, 2> ends{-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends.data()) != 0) {
            throw std::runtime_error(std::string("ned: socketpair: ") + std::strerror(errno));
        }
        return ends;
    }

} // namespace

process::ChildProcess SpawnOnHost(Connection& connection, RemoteSpawn spawn) {
    if (!connection.Connected()) {
        throw std::runtime_error("ned: not connected to " + connection.Destination());
    }
    if (connection.Hello().capabilities.proc < 1) {
        throw std::runtime_error("ned: the agent on " + connection.Destination() + " can't run processes");
    }
    if (spawn.pty) {
        if (connection.Hello().capabilities.proc < host::kProcPtyVersion) {
            throw std::runtime_error("ned: the agent on " + connection.Destination() + " can't run terminals");
        }
        spawn.stderrMode = process::StderrMode::MergeWithStdout;
    }

    const bool         capturesStderr = spawn.stderrMode == process::StderrMode::Capture;
    std::array<int, 2> io             = SocketPair();
    std::array<int, 2> err{-1, -1};
    if (capturesStderr) {
        try {
            err = SocketPair();
        }
        catch (...) {
            ::close(io[0]);
            ::close(io[1]);
            throw;
        }
    }
    const int input = ::fcntl(io[0], F_DUPFD_CLOEXEC, 0);
    if (input < 0) {
        for (const int fd : {io[0], io[1], err[0], err[1]}) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
        throw std::runtime_error(std::string("ned: dup: ") + std::strerror(errno));
    }

    const std::uint64_t    handle = ++g_nextHandle;
    std::shared_ptr<Relay> relay;
    try {
        relay = std::make_shared<Relay>(connection, handle, io[1], err[1], spawn.stderrMode);
    }
    catch (...) {
        for (const int fd : {io[0], input, err[0]}) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
        throw;
    }

    std::ranges::sort(spawn.env);
    const host::ProcSpawnRequest request{.handle     = handle,
                                         .argv       = std::move(spawn.argv),
                                         .cwd        = std::move(spawn.cwd),
                                         .env        = std::move(spawn.env),
                                         .stderrMode = spawn.stderrMode,
                                         .pty        = spawn.pty};
    const std::string            command = request.argv.empty() ? std::string() : request.argv.front();
    relay->request                       = connection.Endpoint().Request(
        std::string(host::kProcSpawn), host::ToJson(request),
        protocol::ResponseHandlers{
            .onResult =
                [relay](const Json& result) {
                    std::optional<int> exit;
                    try {
                        exit = host::ProcExitFromJson(result);
                    }
                    catch (const protocol::ProtocolError& e) {
                        LogMessage(LogCategory::General, LogSeverity::Warning, std::string("ned: a remote process's exit: ") + e.what());
                    }
                    relay->Finish(exit);
                },
            .onError =
                [relay, command, onSpawnFailed = std::move(spawn.onSpawnFailed)](const protocol::RemoteError& error) {
                    if (error.code != host::proc_error::kSpawnFailed) {
                        if (error.code != protocol::error_code::kConnectionClosed) {
                            LogMessage(LogCategory::General, LogSeverity::Warning, "ned: remote " + command + ": " + error.message);
                        }
                        relay->Finish(std::nullopt);
                        return;
                    }
                    const int fd = relay->stderrMode == process::StderrMode::Capture           ? 2
                                   : relay->stderrMode == process::StderrMode::MergeWithStdout ? 1
                                                                                               : 0;
                    if (fd != 0) {
                        const std::lock_guard lock(relay->mutex);
                        relay->out[static_cast<std::size_t>(fd)].push_back(error.message + "\n");
                    }
                    if (onSpawnFailed) {
                        onSpawnFailed(error.message);
                    }
                    relay->Finish(kRemoteSpawnFailed);
                },
            .onChunk =
                [relay](const Json& data) {
                    host::ProcOutput output;
                    try {
                        output = host::ProcOutputFromJson(data);
                    }
                    catch (const protocol::ProtocolError& e) {
                        LogMessage(LogCategory::General, LogSeverity::Warning, std::string("ned: a remote process's output: ") + e.what());
                        relay->Grant(1);
                        return;
                    }
                    {
                        const std::lock_guard lock(relay->mutex);
                        const bool            wanted = output.fd == 1 || relay->err >= 0;
                        if (!relay->released && !relay->discarding && wanted) {
                            relay->out[static_cast<std::size_t>(output.fd)].push_back(std::move(output.data));
                            Notify(relay->wakeFd);
                            return;
                        }
                    }
                    relay->Grant(1);
                },
            .grantsByHand = true});

    return process::ChildProcess(io[0], input, err[0], std::make_unique<RelayControl>(std::move(relay)));
}

} // namespace ned::editor::remote
