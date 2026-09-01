#include "LspBrokerMain.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "BrokerSocketPath.h"
#include "Editor/ProcessTimeouts.h"
#include "LspBroker.h"
#include "Transport.h"

namespace ned::editor::lsp {

namespace {

    // Matches BrokerRouter's own private MakeKey exactly (root + '\x1f' +
    // language) -- duplicated here rather than shared, the same
    // per-file-local-copy convention this codebase already uses for
    // Fnv1a64Hex (Backup.cpp/ProjectSession.cpp/PersistentUndo.cpp/
    // ProjectTrust.cpp each keep their own copy rather than a shared
    // helper).
    std::string ServerKey(const std::string& root, const std::string& language) {
        return root + '\x1f' + language;
    }

    std::size_t CountKind(const std::vector<BrokerAction>& actions, BrokerAction::Kind kind) {
        std::size_t count = 0;
        for (const BrokerAction& action : actions) {
            count += (action.kind == kind) ? 1 : 0;
        }
        return count;
    }

    // lsp-broker verification follow-up. Plain timestamped stderr logging
    // -- the daemon has no config file of its own, so there's no on/off
    // switch here; a caller who wants a persistent record just redirects
    // stderr (`ned --lsp-broker >broker.log 2>&1 &`), same as any ordinary
    // Unix daemon. Deliberately coarse (attach/spawn/evict-or-idle-close/
    // shutdown, not every relayed frame) -- enough to reconstruct what
    // happened without turning this into a full protocol trace.
    void Log(const std::string& message) {
        const auto      now = std::chrono::system_clock::now();
        const std::time_t t  = std::chrono::system_clock::to_time_t(now);
        std::tm          tmBuf{};
        ::localtime_r(&t, &tmBuf);
        char timeBuf[16];
        std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);
        std::cerr << '[' << timeBuf << "] " << message << '\n';
    }

    constexpr auto kIdleSweepInterval      = std::chrono::seconds(60);
    constexpr auto kPerEntryIdleTimeout    = std::chrono::minutes(30);
    constexpr auto kWholeDaemonIdleTimeout = std::chrono::hours(24);
    constexpr int  kClientSendTimeoutSec   = 3;

    // silent-relay-failure-visibility follow-up: a shutdown's own
    // "shutdown"/"exit" writes go through the same ApplySendToServer path
    // as ordinary traffic, so a server that stopped draining its stdin (see
    // ApplySendToServer's own comment) makes shutdown itself slow -- each
    // such write stalls for the full ProtocolStallTimeoutMs before failing.
    // That's a real, bounded wait, not a hang, but with nothing said up
    // front it reads as one. One line stating the worst case up front.
    void LogShutdownEta(const std::vector<BrokerAction>& actions) {
        const std::size_t pending = CountKind(actions, BrokerAction::Kind::SendToServer);
        if (pending == 0) {
            return;
        }
        const auto stallSeconds = std::chrono::duration_cast<std::chrono::seconds>(editor::ProtocolStallTimeoutMs()).count();
        Log("shutting down " + std::to_string(pending) + " server message(s) -- up to " + std::to_string(stallSeconds) +
            "s each if a server has stopped draining its stdin");
    }

    // Owns every live connection (client sockets and real language-server
    // subprocesses) and the one BrokerRouter they're all relayed through.
    //
    // Threading: one jthread per connection, each blocking on its own
    // Transport::ReadFrame() -- LspClient.h's own established shape, minus
    // EventLoop::Post (there is none here; a plain mutex_ guards router_
    // and both connection maps instead). Every router_ call is made while
    // holding mutex_; WriteFrame calls are made *outside* it (a raw
    // pointer is fetched under a short-held lock, then used after
    // releasing it) so one slow/wedged peer's blocking write can't stall
    // every other connection's routing.
    //
    // Lifetime/erase discipline: a connection's own reader thread is the
    // *only* thread that ever erases that connection's own map entry
    // (clientTransports_[connId] / serverTransports_[key]) -- CloseClient/
    // CloseServer actions applied from a *different* thread erase the
    // entry directly instead (destroying the Transport, closing its fds),
    // which is what interrupts that connection's own blocking
    // ReadFrame() call via EOF/error, the exact same cross-thread
    // "destroy the Transport to interrupt a blocked read" idiom
    // LspClient.h's own header comment documents and this codebase already
    // relies on at real shutdown. The one place this doesn't fully apply
    // is a genuinely hung language server that ignores "exit" and never
    // closes its pipes on its own -- a known, accepted v1 gap (that one
    // reader thread stays blocked until the whole daemon process exits,
    // which reclaims it; no other project/language is affected either
    // way).
    //
    // A reader thread's own std::jthread handle (in clientReaderThreads_/
    // serverReaderThreads_) is *not* erased by that same thread -- a
    // thread cannot join itself. ReapFinishedThreads(), called from the
    // periodic idle-sweep tick, erases (and so joins -- instantly, since
    // the thread has already finished by then) any reader-thread handle
    // whose connection/server entry is already gone from the transport
    // maps.
    class BrokerDaemon {
      public:
        explicit BrokerDaemon(int maxConcurrentServers) : router_(maxConcurrentServers) {
        }

        int Run() {
            EnsureBrokerRuntimeDirectory();
            listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (listenFd_ < 0) {
                std::cerr << "ned: lsp-broker: socket() failed: " << std::strerror(errno) << "\n";
                return 1;
            }
            const std::string socketPathStr = BrokerSocketPath().string();
            sockaddr_un       addr{};
            if (socketPathStr.size() >= sizeof(addr.sun_path)) {
                std::cerr << "ned: lsp-broker: socket path too long: " << socketPathStr << "\n";
                return 1;
            }
            ::unlink(socketPathStr.c_str()); // a stale socket from a crashed prior daemon -- safe, we're about to bind fresh under the lock file's protection (see main.cpp's own auto-spawn path)
            addr.sun_family = AF_UNIX;
            std::strncpy(addr.sun_path, socketPathStr.c_str(), sizeof(addr.sun_path) - 1);
            if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
                std::cerr << "ned: lsp-broker: bind() failed: " << std::strerror(errno) << "\n";
                ::close(listenFd_);
                return 1;
            }
            if (::listen(listenFd_, 16) != 0) {
                std::cerr << "ned: lsp-broker: listen() failed: " << std::strerror(errno) << "\n";
                ::close(listenFd_);
                return 1;
            }
            Log("daemon started, listening at " + socketPathStr);

            std::jthread idleThread([this](std::stop_token stopToken) { IdleSweepLoop(stopToken); });

            while (!shuttingDown_) {
                const int clientFd = ::accept(listenFd_, nullptr, nullptr);
                if (clientFd < 0) {
                    if (shuttingDown_) {
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    break; // the listen socket was closed (ShutdownProcess) or a fatal error -- stop accepting either way
                }
                const timeval sendTimeout{.tv_sec = kClientSendTimeoutSec, .tv_usec = 0};
                ::setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));

                const ConnectionId connId = nextConnectionId_++;
                std::lock_guard<std::mutex> lock(mutex_);
                clientReaderThreads_.emplace(connId, std::jthread([this, connId, clientFd] { HandleClientConnection(connId, clientFd); }));
            }

            idleThread.request_stop();
            // Every remaining reader thread is joined by the normal
            // container destruction below -- each either already observed
            // EOF (from ShutdownProcess's own preceding CloseClient/
            // CloseServer actions, applied in order before ShutdownProcess
            // is ever reached) or will shortly.
            ::unlink(socketPathStr.c_str()); // tidiness only -- the next daemon startup already unlinks a stale socket unconditionally before binding
            Log("daemon exiting");
            return 0;
        }

      private:
        void ApplyActions(std::vector<BrokerAction> actions) {
            for (const BrokerAction& action : actions) {
                switch (action.kind) {
                    case BrokerAction::Kind::SpawnServer: ApplySpawnServer(action); break;
                    case BrokerAction::Kind::SendToServer: ApplySendToServer(action); break;
                    case BrokerAction::Kind::SendToClient: ApplySendToClient(action); break;
                    case BrokerAction::Kind::CloseClient: {
                        std::lock_guard<std::mutex> lock(mutex_);
                        clientTransports_.erase(action.connection);
                        break;
                    }
                    case BrokerAction::Kind::CloseServer: {
                        std::lock_guard<std::mutex> lock(mutex_);
                        serverTransports_.erase(ServerKey(action.root, action.language));
                        break;
                    }
                    case BrokerAction::Kind::ShutdownProcess: {
                        shuttingDown_ = true;
                        // shutdown(), not close() -- confirmed live: a
                        // plain close() of a listening socket does *not*
                        // reliably unblock a different thread's pending
                        // accept() on Linux (unlike closing a connected
                        // stream fd out from under a blocked read(), which
                        // is reliable and is exactly what CloseClient/
                        // CloseServer above rely on). shutdown(SHUT_RDWR)
                        // on the listening socket is the documented,
                        // actually-reliable way to force a blocked
                        // accept() to return.
                        ::shutdown(listenFd_, SHUT_RDWR);
                        ::close(listenFd_);
                        break;
                    }
                }
            }
        }

        void ApplySpawnServer(const BrokerAction& action) {
            std::unique_ptr<lsp::Transport> transport;
            try {
                transport = std::make_unique<lsp::Transport>(action.argv, /*captureStderr=*/false);
                // Stderr capture/log rollup (the *lsp log* buffer real
                // LspClient sessions feed) isn't wired for broker-spawned
                // servers in v1 -- a documented gap, not an oversight; see
                // this file's own header comment on why the graceful
                // shutdown/exit story took priority for the initial slice.
            }
            catch (const std::exception& e) {
                Log("spawn FAILED root=" + action.root + " language=" + action.language + " error=" + e.what());
                std::vector<BrokerAction> failActions;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    failActions = router_.ServerSpawnFailed(action.root, action.language, e.what());
                }
                ApplyActions(std::move(failActions));
                return;
            }
            Log("spawned root=" + action.root + " language=" + action.language + " pid=" + std::to_string(transport->Pid()));
            const std::string key = ServerKey(action.root, action.language);
            std::vector<BrokerAction> spawnedActions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                serverTransports_[key] = std::move(transport);
                serverReaderThreads_.emplace(key, std::jthread([this, root = action.root, language = action.language] { ServerReadLoop(root, language); }));
                spawnedActions = router_.ServerSpawned(action.root, action.language);
            }
            ApplyActions(std::move(spawnedActions));
        }

        void ApplySendToServer(const BrokerAction& action) {
            lsp::Transport* target = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (const auto it = serverTransports_.find(ServerKey(action.root, action.language)); it != serverTransports_.end()) {
                    target = it->second.get();
                }
            }
            if (target == nullptr) {
                // silent-relay-failure-visibility follow-up: previously a
                // bare `return` here -- a request routed to a server entry
                // whose transport had already vanished from serverTransports_
                // (while languages_ still thinks it's Ready/attached) used to
                // disappear with zero trace, surfacing only as the client's
                // own 30s "request timed out" much later with nothing in this
                // log to explain why. Logged, not fixed here -- this is a
                // diagnostic, not a guess at the root cause.
                Log("SendToServer dropped (no live server transport) root=" + action.root + " language=" + action.language);
                return;
            }
            try {
                target->WriteFrame(action.frame.dump());
            }
            catch (const std::exception& e) {
                // silent-relay-failure-visibility follow-up: was a silent
                // best-effort catch. Still best-effort (the reader
                // thread's own EOF path still does the actual cleanup), but
                // now leaves a trace instead of vanishing without one.
                Log("SendToServer write failed root=" + action.root + " language=" + action.language + " error=" + e.what());
            }
        }

        void ApplySendToClient(const BrokerAction& action) {
            lsp::Transport* target = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (const auto it = clientTransports_.find(action.connection); it != clientTransports_.end()) {
                    target = it->second.get();
                }
            }
            if (target == nullptr) {
                // silent-relay-failure-visibility follow-up: see
                // ApplySendToServer's own comment above -- same gap, same fix.
                Log("SendToClient dropped (no live client transport) conn=" + std::to_string(action.connection));
                return;
            }
            try {
                target->WriteFrame(action.frame.dump());
            }
            catch (const std::exception& e) {
                // silent-relay-failure-visibility follow-up: see
                // ApplySendToServer's own comment above -- same gap, same fix.
                // SO_SNDTIMEO (set at accept()) bounds how long this can ever
                // block before throwing.
                Log("SendToClient write failed conn=" + std::to_string(action.connection) + " error=" + e.what());
            }
        }

        void HandleClientConnection(ConnectionId connId, int fd) {
            const int dupFd = ::dup(fd); // PtyProcess.cpp's own precedent: ChildProcess's two fds must be independently closeable, never the same fd twice
            auto      transport = std::make_unique<lsp::Transport>(fd, dupFd, -1);

            std::optional<std::string> firstFrameText;
            try {
                firstFrameText = transport->ReadFrame();
            }
            catch (const std::exception&) {
                return;
            }
            if (!firstFrameText) {
                return;
            }
            Json firstFrame;
            try {
                firstFrame = Json::parse(*firstFrameText);
            }
            catch (const std::exception&) {
                return;
            }
            const std::string method = firstFrame.value("method", std::string());

            if (method == "ned/broker-shutdown") {
                Log("received ned/broker-shutdown -- shutting down");
                std::vector<BrokerAction> actions;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    actions = router_.Shutdown();
                }
                LogShutdownEta(actions);
                ApplyActions(std::move(actions));
                return; // never attached -- nothing to erase; transport destructs here, closing this control connection's own fds
            }
            if (method != "ned/broker-attach") {
                return; // protocol violation -- drop
            }

            const Json                params = firstFrame.value("params", Json::object());
            const std::string         root = params.value("projectRoot", std::string());
            const std::string         language = params.value("language", std::string());
            std::vector<std::string>  argv;
            if (params.contains("argv") && params.at("argv").is_array()) {
                for (const auto& item : params.at("argv")) {
                    if (item.is_string()) {
                        argv.push_back(item.get<std::string>());
                    }
                }
            }

            Log("attach conn=" + std::to_string(connId) + " root=" + root + " language=" + language);
            std::vector<BrokerAction> attachActions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                clientTransports_[connId] = std::move(transport);
                attachActions             = router_.ClientAttached(connId, root, language, argv);
            }
            if (const std::size_t evicted = CountKind(attachActions, BrokerAction::Kind::CloseServer); evicted > 0) {
                Log("this attach triggered eviction of " + std::to_string(evicted) + " idle entry/entries (at capacity)");
            }
            ApplyActions(std::move(attachActions));

            while (true) {
                lsp::Transport* liveTransport = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto                  it = clientTransports_.find(connId);
                    if (it == clientTransports_.end()) {
                        break; // closed by someone else (eviction/idle-timeout/crash teardown/whole-daemon shutdown)
                    }
                    liveTransport = it->second.get();
                }
                std::optional<std::string> frameText;
                try {
                    frameText = liveTransport->ReadFrame();
                }
                catch (const std::exception&) {
                    break;
                }
                if (!frameText) {
                    break;
                }
                Json frame;
                try {
                    frame = Json::parse(*frameText);
                }
                catch (const std::exception&) {
                    continue; // malformed frame from this client -- ignore it, keep reading
                }
                std::vector<BrokerAction> actions;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    actions = router_.ClientFrame(connId, frame);
                }
                ApplyActions(std::move(actions));
            }

            Log("disconnect conn=" + std::to_string(connId));
            std::vector<BrokerAction> disconnectActions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                clientTransports_.erase(connId); // idempotent -- already gone if someone else closed us
                disconnectActions = router_.ClientDisconnected(connId);
            }
            ApplyActions(std::move(disconnectActions));
        }

        void ServerReadLoop(const std::string& root, const std::string& language) {
            const std::string key = ServerKey(root, language);
            while (true) {
                lsp::Transport* liveTransport = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto                  it = serverTransports_.find(key);
                    if (it == serverTransports_.end()) {
                        break;
                    }
                    liveTransport = it->second.get();
                }
                std::optional<std::string> frameText;
                try {
                    frameText = liveTransport->ReadFrame();
                }
                catch (const std::exception&) {
                    break;
                }
                if (!frameText) {
                    break;
                }
                Json frame;
                try {
                    frame = Json::parse(*frameText);
                }
                catch (const std::exception&) {
                    continue;
                }
                std::vector<BrokerAction> actions;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    actions = router_.ServerFrame(root, language, frame);
                }
                ApplyActions(std::move(actions));
            }

            Log("server connection ended root=" + root + " language=" + language);
            std::vector<BrokerAction> disconnectActions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                serverTransports_.erase(key); // idempotent -- already gone if a CloseServer action beat us here
                disconnectActions = router_.ServerDisconnected(root, language);
            }
            ApplyActions(std::move(disconnectActions));
        }

        void ReapFinishedThreads() {
            std::lock_guard<std::mutex> lock(mutex_);
            std::erase_if(clientReaderThreads_, [this](const auto& entry) { return !clientTransports_.contains(entry.first); });
            std::erase_if(serverReaderThreads_, [this](const auto& entry) { return !serverTransports_.contains(entry.first); });
        }

        void IdleSweepLoop(std::stop_token stopToken) {
            auto lastActivitySeen = std::chrono::steady_clock::now();
            while (!stopToken.stop_requested()) {
                for (int waited = 0; waited < kIdleSweepInterval.count() && !stopToken.stop_requested(); ++waited) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                if (stopToken.stop_requested()) {
                    break;
                }

                const auto                now = std::chrono::steady_clock::now();
                std::vector<BrokerAction> actions;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    actions = router_.IdleSweep(now, kPerEntryIdleTimeout);
                    if (router_.ConnectionCount() > 0) {
                        lastActivitySeen = now;
                    }
                }
                if (const std::size_t closed = CountKind(actions, BrokerAction::Kind::CloseServer); closed > 0) {
                    Log("idle-timeout sweep closed " + std::to_string(closed) + " entry/entries (idle past " +
                        std::to_string(std::chrono::duration_cast<std::chrono::minutes>(kPerEntryIdleTimeout).count()) + " min)");
                }
                ApplyActions(std::move(actions));
                ReapFinishedThreads();

                if (now - lastActivitySeen > kWholeDaemonIdleTimeout) {
                    Log("whole-daemon idle timeout reached (no connections for " +
                        std::to_string(std::chrono::duration_cast<std::chrono::hours>(kWholeDaemonIdleTimeout).count()) + "h) -- shutting down");
                    std::vector<BrokerAction> shutdownActions;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        shutdownActions = router_.Shutdown();
                    }
                    LogShutdownEta(shutdownActions);
                    ApplyActions(std::move(shutdownActions));
                    break;
                }
            }
        }

        std::mutex                                                mutex_; // guards router_ and every map below
        BrokerRouter                                               router_;
        std::unordered_map<ConnectionId, std::unique_ptr<lsp::Transport>> clientTransports_;
        std::unordered_map<std::string, std::unique_ptr<lsp::Transport>>  serverTransports_;
        std::unordered_map<ConnectionId, std::jthread>            clientReaderThreads_;
        std::unordered_map<std::string, std::jthread>             serverReaderThreads_;
        std::atomic<ConnectionId>                                 nextConnectionId_{1};
        int                                                        listenFd_ = -1;
        std::atomic<bool>                                         shuttingDown_{false};
    };

} // namespace

int RunLspBrokerDaemon(int maxConcurrentServers) {
    // The daemon writes to many sockets/pipes that routinely close out from
    // under it (an evicted/crashed/disconnected peer), and an unhandled
    // SIGPIPE's default action is to terminate the *entire* daemon over one
    // bad write, taking down every other project's warm session with it.
    // write()/send() already return EPIPE instead, which
    // Transport::WriteFrame already surfaces as a caught std::runtime_error.
    // async-write-queue follow-up: main.cpp's own main() now sets the exact
    // same disposition, for the same reason -- an earlier version of this
    // comment claimed the interactive editor didn't need it (Notcurses'
    // terminal setup supposedly shielding it); that was never verified and
    // turned out false, so don't assume this process is a special case
    // either.
    std::signal(SIGPIPE, SIG_IGN);

    BrokerDaemon daemon(maxConcurrentServers);
    return daemon.Run();
}

} // namespace ned::editor::lsp
