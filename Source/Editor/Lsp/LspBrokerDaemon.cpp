#include "LspBrokerDaemon.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <iostream>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "BrokerSocketPath.h"
#include "Editor/ProcessTimeouts.h"

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
    // switch here. A manually-launched `ned --lsp-broker` inherits its
    // caller's own stderr unmodified, same as any ordinary Unix command
    // (redirect it yourself: `ned --lsp-broker >broker.log 2>&1 &`); the
    // auto-spawned daemon (the common case -- LspBrokerConnect.cpp's own
    // TryBecomeBrokerSpawner) redirects stdout/stderr to
    // BrokerLogPath() (BrokerSocketPath.h) right after fork(), before
    // exec, so this output lands there instead of leaking indefinitely
    // into whichever terminal happened to trigger the first LSP
    // connection after a fresh boot/login (broker-log-redirect
    // follow-up -- confirmed live, not hypothetical). Deliberately coarse
    // (attach/spawn/evict-or-idle-close/shutdown, not every relayed
    // frame) -- enough to reconstruct what happened without turning this
    // into a full protocol trace.
    void Log(const std::string& message) {
        const auto        now = std::chrono::system_clock::now();
        const std::time_t t   = std::chrono::system_clock::to_time_t(now);
        std::tm           tmBuf{};
        ::localtime_r(&t, &tmBuf);
        char timeBuf[16];
        std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);
        std::cerr << '[' << timeBuf << "] " << message << '\n';
    }

    constexpr int kClientSendTimeoutSec = 3;

    // silent-relay-failure-visibility follow-up: a shutdown's own
    // "shutdown"/"exit" writes go through the same ApplySendToServer path
    // as ordinary traffic, so a server that stopped draining its stdin (see
    // ApplySendToServer's own comment) makes shutdown itself slow -- each
    // such write stalls for the full ProtocolWriteStallTimeoutMs before
    // failing. That's a real, bounded wait, not a hang, but with nothing
    // said up front it reads as one. One line stating the worst case up
    // front.
    void LogShutdownEta(const std::vector<BrokerAction>& actions) {
        const std::size_t pending = CountKind(actions, BrokerAction::Kind::SendToServer);
        if (pending == 0) {
            return;
        }
        const auto stallSeconds = std::chrono::duration_cast<std::chrono::seconds>(editor::ProtocolWriteStallTimeoutMs()).count();
        Log("shutting down " + std::to_string(pending) + " server message(s) -- up to " + std::to_string(stallSeconds) +
            "s each if a server has stopped draining its stdin");
    }

    // stale-broker-shutdown follow-up, Linux-specific (see ROADMAP.md's
    // Native Windows Port sketch for why): `/proc/self/exe` is a magic
    // symlink tracking the specific inode this process is actually
    // executing, independent of what a later rename/overwrite does to the
    // pathname. Resolved once at startup to a real path, then re-`stat`ed on
    // every idle sweep and compared against the (device, inode, mtime)
    // recorded at startup -- comparing all three catches both a linker's
    // typical "write to a temp file, rename over the target" (changes the
    // inode) and a build process that instead truncates and overwrites the
    // same inode in place (leaves the inode alone but bumps mtime). A
    // missing/unstattable path is treated as "changed" too (rare, but more
    // likely a sign something's wrong than a reason to keep running
    // silently).
    std::optional<std::filesystem::path> ResolveOwnExecutablePath() {
        std::error_code       ec;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::nullopt : std::make_optional(std::move(path));
    }

    struct stat StatOrZero(const std::filesystem::path& path, bool& ok) {
        struct stat info{};
        ok = ::stat(path.c_str(), &info) == 0;
        return info;
    }

    // How long one sleep inside the sweep's own wait loop lasts -- the wait
    // is chopped into these so a stop request is noticed promptly even when
    // idleSweepInterval is the production 10s.
    constexpr auto kSweepWaitGranularity = std::chrono::milliseconds(200);

} // namespace

BrokerDaemon::BrokerDaemon(BrokerDaemonOptions options) : options_(std::move(options)), router_(options_.maxConcurrentServers) {
}

BrokerDaemon::BrokerDaemon(int maxConcurrentServers) : BrokerDaemon(BrokerDaemonOptions{.maxConcurrentServers = maxConcurrentServers}) {
}

BrokerDaemon::~BrokerDaemon() {
    // Close every remaining connection before joining anything: a reader
    // thread parked in ReadFrame() only returns once its own connection is
    // closed, so joining first (which is all plain member destruction would
    // do) could wait forever on a peer that has nothing left to say.
    // Closing is safe with a reader mid-call -- that is exactly what
    // Transport::Close() exists for.
    std::vector<TransportPtr> closing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [connId, transport] : clientTransports_) {
            closing.push_back(transport);
        }
        for (const auto& [key, transport] : serverTransports_) {
            closing.push_back(transport);
        }
    }
    for (const TransportPtr& transport : closing) {
        CloseTransport(transport);
    }
    closing.clear();

    // Then the joins, with mutex_ free so each reader's own tail can run to
    // completion. Explicit rather than left to member destruction so the
    // ordering is stated where it can be seen, not merely implied by
    // declaration order (which backs it up too -- see the header).
    clientReaderThreads_.clear();
    serverReaderThreads_.clear();
}

int BrokerDaemon::Run() {
    // stale-broker-shutdown follow-up: captured before anything else so the
    // baseline reflects the exact binary this process is executing, not
    // whatever happens to be at that path by the time the first idle sweep
    // runs. A resolution/stat failure (no /proc, sandboxed environment, ...)
    // just disables this check silently -- the whole-daemon idle timeout
    // still applies regardless.
    if (options_.watchExecutableIdentity) {
        exePath_ = ResolveOwnExecutablePath();
        if (exePath_) {
            bool              ok   = false;
            const struct stat info = StatOrZero(*exePath_, ok);
            if (ok) {
                exeIdentityAtStartup_ =
                    ExecutableIdentity{.device = info.st_dev, .inode = info.st_ino, .modifiedAt = info.st_mtime};
            }
        }
    }

    std::string socketPathStr;
    if (options_.socketPath.empty()) {
        EnsureBrokerRuntimeDirectory();
        socketPathStr = BrokerSocketPath().string();
    }
    else {
        socketPathStr = options_.socketPath.string();
    }

    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "ned: lsp-broker: socket() failed: " << std::strerror(errno) << "\n";
        return 1;
    }
    sockaddr_un addr{};
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

        const ConnectionId          connId = nextConnectionId_++;
        std::lock_guard<std::mutex> lock(mutex_);
        clientReaderThreads_.emplace(connId, std::jthread([this, connId, clientFd] { HandleClientConnection(connId, clientFd); }));
    }

    idleThread.request_stop();
    // Every remaining reader thread is joined by the normal container
    // destruction below -- each either already observed EOF (from
    // ShutdownProcess's own preceding CloseClient/CloseServer actions,
    // applied in order before ShutdownProcess is ever reached) or will
    // shortly.
    ::unlink(socketPathStr.c_str()); // tidiness only -- the next daemon startup already unlinks a stale socket unconditionally before binding
    Log("daemon exiting");
    return 0;
}

void BrokerDaemon::CloseTransport(TransportPtr transport) {
    // broker-reader-deadlock follow-up: closing, not destroying, is what
    // wakes a reader parked in ReadFrame() -- and unlike destroying it,
    // that reader is guaranteed to return into an object that still
    // exists, since it holds its own strong reference. The last reference
    // (usually the reader's own, right after it returns) runs the real
    // teardown, including reaping the child process.
    if (transport) {
        transport->Close();
    }
}

void BrokerDaemon::ApplyActions(std::vector<BrokerAction> actions) {
    for (const BrokerAction& action : actions) {
        switch (action.kind) {
            case BrokerAction::Kind::SpawnServer:
                ApplySpawnServer(action);
                break;
            case BrokerAction::Kind::SendToServer:
                ApplySendToServer(action);
                break;
            case BrokerAction::Kind::SendToClient:
                ApplySendToClient(action);
                break;
            case BrokerAction::Kind::CloseClient: {
                TransportPtr closing;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (const auto it = clientTransports_.find(action.connection); it != clientTransports_.end()) {
                        closing = std::move(it->second);
                        clientTransports_.erase(it);
                    }
                }
                CloseTransport(std::move(closing)); // outside the lock -- see CloseTransport
                break;
            }
            case BrokerAction::Kind::CloseServer: {
                TransportPtr closing;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (const auto it = serverTransports_.find(ServerKey(action.root, action.language)); it != serverTransports_.end()) {
                        closing = std::move(it->second);
                        serverTransports_.erase(it);
                    }
                }
                CloseTransport(std::move(closing));
                break;
            }
            case BrokerAction::Kind::ShutdownProcess: {
                shuttingDown_ = true;
                // shutdown(), not close() -- confirmed live: a plain
                // close() of a listening socket does *not* reliably unblock
                // a different thread's pending accept() on Linux (unlike
                // closing a connected stream fd out from under a blocked
                // read(), which is reliable and is exactly what
                // CloseClient/CloseServer above rely on).
                // shutdown(SHUT_RDWR) on the listening socket is the
                // documented, actually-reliable way to force a blocked
                // accept() to return.
                ::shutdown(listenFd_, SHUT_RDWR);
                ::close(listenFd_);
                break;
            }
        }
    }
}

void BrokerDaemon::ApplySpawnServer(const BrokerAction& action) {
    TransportPtr transport;
    try {
        transport = std::make_shared<lsp::Transport>(action.argv, /*captureStderr=*/false);
        // Stderr capture/log rollup (the *lsp log* buffer real LspClient
        // sessions feed) isn't wired for broker-spawned servers in v1 -- a
        // documented gap, not an oversight.
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
    const std::string         key = ServerKey(action.root, action.language);
    std::vector<BrokerAction> spawnedActions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        serverTransports_[key] = std::move(transport);
        finishedServerReaders_.erase(key); // a previous incarnation's announcement must not reap this new thread
        serverReaderThreads_.insert_or_assign(
            key, std::jthread([this, root = action.root, language = action.language] { ServerReadLoop(root, language); }));
        spawnedActions = router_.ServerSpawned(action.root, action.language);
    }
    ApplyActions(std::move(spawnedActions));
}

void BrokerDaemon::ApplySendToServer(const BrokerAction& action) {
    TransportPtr target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = serverTransports_.find(ServerKey(action.root, action.language)); it != serverTransports_.end()) {
            target = it->second; // a strong reference, so the write below can't race a concurrent close/destroy
        }
    }
    if (!target) {
        // silent-relay-failure-visibility follow-up: previously a bare
        // `return` here -- a request routed to a server entry whose
        // transport had already vanished from serverTransports_ (while
        // languages_ still thinks it's Ready/attached) used to disappear
        // with zero trace, surfacing only as the client's own 30s "request
        // timed out" much later with nothing in this log to explain why.
        // Logged, not fixed here -- this is a diagnostic, not a guess at the
        // root cause.
        Log("SendToServer dropped (no live server transport) root=" + action.root + " language=" + action.language);
        return;
    }
    try {
        target->WriteFrame(action.frame.dump());
    }
    catch (const std::exception& e) {
        // silent-relay-failure-visibility follow-up: was a silent
        // best-effort catch. Still best-effort (the reader thread's own EOF
        // path still does the actual cleanup), but now leaves a trace
        // instead of vanishing without one.
        Log("SendToServer write failed root=" + action.root + " language=" + action.language + " error=" + e.what());
    }
}

void BrokerDaemon::ApplySendToClient(const BrokerAction& action) {
    TransportPtr target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = clientTransports_.find(action.connection); it != clientTransports_.end()) {
            target = it->second;
        }
    }
    if (!target) {
        // silent-relay-failure-visibility follow-up: see ApplySendToServer's
        // own comment above -- same gap, same fix.
        Log("SendToClient dropped (no live client transport) conn=" + std::to_string(action.connection));
        return;
    }
    try {
        target->WriteFrame(action.frame.dump());
    }
    catch (const std::exception& e) {
        // SO_SNDTIMEO (set at accept()) bounds how long this can ever block
        // before throwing.
        Log("SendToClient write failed conn=" + std::to_string(action.connection) + " error=" + e.what());
    }
}

void BrokerDaemon::ClientReaderFinished(ConnectionId connId) {
    std::lock_guard<std::mutex> lock(mutex_);
    finishedClientReaders_.insert(connId);
}

void BrokerDaemon::ServerReaderFinished(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    finishedServerReaders_.insert(key);
}

void BrokerDaemon::HandleClientConnection(ConnectionId connId, int fd) {
    const int    dupFd     = ::dup(fd); // PtyProcess.cpp's own precedent: ChildProcess's two fds must be independently closeable, never the same fd twice
    TransportPtr transport = std::make_shared<lsp::Transport>(fd, dupFd, -1);

    std::optional<std::string> firstFrameText;
    try {
        firstFrameText = transport->ReadFrame();
    }
    catch (const std::exception&) {
        ClientReaderFinished(connId);
        return;
    }
    if (!firstFrameText) {
        ClientReaderFinished(connId);
        return;
    }
    Json firstFrame;
    try {
        firstFrame = Json::parse(*firstFrameText);
    }
    catch (const std::exception&) {
        ClientReaderFinished(connId);
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
        ClientReaderFinished(connId);
        return; // never attached -- nothing to erase; the transport destructs here, closing this control connection's own fds
    }
    if (method != "ned/broker-attach") {
        ClientReaderFinished(connId);
        return; // protocol violation -- drop
    }

    const Json               params   = firstFrame.value("params", Json::object());
    const std::string        root     = params.value("projectRoot", std::string());
    const std::string        language = params.value("language", std::string());
    std::vector<std::string> argv;
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
        clientTransports_[connId] = transport;
        attachActions             = router_.ClientAttached(connId, root, language, argv);
    }
    if (const std::size_t evicted = CountKind(attachActions, BrokerAction::Kind::CloseServer); evicted > 0) {
        Log("this attach triggered eviction of " + std::to_string(evicted) + " idle entry/entries (at capacity)");
    }
    ApplyActions(std::move(attachActions));

    while (true) {
        TransportPtr liveTransport;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto                  it = clientTransports_.find(connId);
            if (it == clientTransports_.end()) {
                break; // closed by someone else (eviction/idle-timeout/crash teardown/whole-daemon shutdown)
            }
            liveTransport = it->second; // strong reference held for the whole blocking read below
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
    TransportPtr              closing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = clientTransports_.find(connId); it != clientTransports_.end()) {
            closing = std::move(it->second);
            clientTransports_.erase(it); // idempotent -- already gone if someone else closed us
        }
        disconnectActions = router_.ClientDisconnected(connId);
    }
    CloseTransport(std::move(closing));
    ApplyActions(std::move(disconnectActions));
    ClientReaderFinished(connId); // last act -- see this class's lifetime/erase-discipline comment
}

void BrokerDaemon::ServerReadLoop(const std::string& root, const std::string& language) {
    const std::string key = ServerKey(root, language);
    while (true) {
        TransportPtr liveTransport;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto                  it = serverTransports_.find(key);
            if (it == serverTransports_.end()) {
                break;
            }
            liveTransport = it->second; // strong reference held for the whole blocking read below
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
    TransportPtr              closing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const auto it = serverTransports_.find(key); it != serverTransports_.end()) {
            closing = std::move(it->second);
            serverTransports_.erase(it); // idempotent -- already gone if a CloseServer action beat us here
        }
        disconnectActions = router_.ServerDisconnected(root, language);
    }
    CloseTransport(std::move(closing));
    ApplyActions(std::move(disconnectActions));
    ServerReaderFinished(key); // last act -- see this class's lifetime/erase-discipline comment
}

void BrokerDaemon::ReapFinishedThreads() {
    // broker-reader-deadlock follow-up. Two rules, both load-bearing:
    // only announced (genuinely finished) threads are joined, and the joins
    // themselves happen after mutex_ is released. Violating either one is
    // what wedged the whole daemon before -- see this class's own
    // lifetime/erase-discipline comment.
    std::vector<std::jthread> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const ConnectionId connId : finishedClientReaders_) {
            if (const auto it = clientReaderThreads_.find(connId); it != clientReaderThreads_.end()) {
                finished.push_back(std::move(it->second));
                clientReaderThreads_.erase(it);
            }
        }
        finishedClientReaders_.clear();
        for (const std::string& key : finishedServerReaders_) {
            if (const auto it = serverReaderThreads_.find(key); it != serverReaderThreads_.end()) {
                finished.push_back(std::move(it->second));
                serverReaderThreads_.erase(it);
            }
        }
        finishedServerReaders_.clear();
    }
    finished.clear(); // the joins, with mutex_ released
}

void BrokerDaemon::IdleSweepLoop(std::stop_token stopToken) {
    auto       lastActivitySeen = std::chrono::steady_clock::now();
    const auto tick             = std::min(options_.idleSweepInterval, kSweepWaitGranularity);
    while (!stopToken.stop_requested()) {
        for (auto waited = std::chrono::milliseconds(0); waited < options_.idleSweepInterval && !stopToken.stop_requested();
             waited += tick) {
            std::this_thread::sleep_for(tick);
        }
        if (stopToken.stop_requested()) {
            break;
        }

        const auto                now = std::chrono::steady_clock::now();
        std::vector<BrokerAction> actions;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            actions = router_.IdleSweep(now, std::chrono::duration_cast<std::chrono::milliseconds>(options_.perEntryIdleTimeout));
            if (router_.ConnectionCount() > 0) {
                lastActivitySeen = now;
            }
        }
        if (const std::size_t closed = CountKind(actions, BrokerAction::Kind::CloseServer); closed > 0) {
            Log("idle-timeout sweep closed " + std::to_string(closed) + " entry/entries (idle past " +
                std::to_string(std::chrono::duration_cast<std::chrono::seconds>(options_.perEntryIdleTimeout).count()) + "s)");
        }
        ApplyActions(std::move(actions));
        ReapFinishedThreads();

        if (now - lastActivitySeen > options_.wholeDaemonIdleTimeout) {
            Log("whole-daemon idle timeout reached (no connections for " +
                std::to_string(std::chrono::duration_cast<std::chrono::seconds>(options_.wholeDaemonIdleTimeout).count()) +
                "s) -- shutting down");
            std::vector<BrokerAction> shutdownActions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                shutdownActions = router_.Shutdown();
            }
            LogShutdownEta(shutdownActions);
            ApplyActions(std::move(shutdownActions));
            break;
        }

        // stale-broker-shutdown follow-up: unlike the idle timeout above,
        // checked regardless of connection count -- a project left open in a
        // long-running `ned` session must not block this daemon from
        // noticing its own binary was rebuilt out from under it.
        if (exePath_ && exeIdentityAtStartup_) {
            bool                     ok      = false;
            const struct stat        info    = StatOrZero(*exePath_, ok);
            const ExecutableIdentity current = ExecutableIdentity{.device = info.st_dev, .inode = info.st_ino, .modifiedAt = info.st_mtime};
            if (!ok || !(current == *exeIdentityAtStartup_)) {
                Log("executable changed on disk (" + exePath_->string() + ") -- shutting down for restart");
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
}

} // namespace ned::editor::lsp
