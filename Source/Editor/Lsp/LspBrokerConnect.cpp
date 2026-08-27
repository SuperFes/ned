#include "LspBrokerConnect.h"

#include <chrono>
#include <climits>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "BrokerSocketPath.h"
#include "Editor/Process/ChildProcess.h"
#include "Transport.h"

// posix_spawn's envp argument needs the process's own environment -- POSIX
// guarantees this global exists, just not in a standard header. Same
// declaration ChildProcess.cpp already carries for the same reason.
extern char** environ;

namespace ned::editor::lsp {

namespace {

    // editor-side-connect-timeout follow-up: confirmed live -- a plain
    // blocking ::connect() to the broker's Unix socket froze a real,
    // interactive `ned` process's *main UI thread* solid (unix_wait_for_peer
    // in /proc, gdb-inaccessible since it never returns) the moment the
    // daemon's listen(2) backlog (fixed at 16, LspBrokerMain.cpp) filled up
    // and its accept loop stalled -- every "attach" call in this codebase
    // runs synchronously on LspManager::ClientForLanguage <- SyncBuffer <-
    // BufferView::Paint(), i.e. the main thread, exactly the header
    // comment above already says. A stalled/overloaded broker used to mean
    // an unkillable-by-the-user-except-via-kill-9 editor; this makes it
    // mean "falls back to a direct spawn, like the daemon was never there,"
    // the exact behavior a plain ECONNREFUSED already gets.
    constexpr int kConnectTimeoutMs = 300;

    // Non-blocking connect + poll(POLLOUT) + SO_ERROR is the standard
    // POSIX pattern for a connect() that can never hang past timeoutMs,
    // for a stream socket of any address family including AF_UNIX. fd is
    // left blocking again on success (every downstream Transport/ReadFrame
    // call in this codebase assumes a blocking fd, matching ChildProcess's
    // own convention) -- on any failure path the caller closes fd itself,
    // so its blocking-mode isn't restored there.
    bool ConnectWithTimeout(int fd, const sockaddr* addr, socklen_t addrLen, int timeoutMs) {
        const int originalFlags = ::fcntl(fd, F_GETFL, 0);
        if (originalFlags < 0 || ::fcntl(fd, F_SETFL, originalFlags | O_NONBLOCK) < 0) {
            return false;
        }
        if (::connect(fd, addr, addrLen) == 0) {
            ::fcntl(fd, F_SETFL, originalFlags);
            return true;
        }
        if (errno != EINPROGRESS) {
            return false;
        }
        pollfd pfd{.fd = fd, .events = POLLOUT, .revents = 0};
        if (::poll(&pfd, 1, timeoutMs) <= 0) {
            return false; // timed out (backlog full / daemon wedged) or poll() itself failed -- same outcome either way
        }
        int       socketError = 0;
        socklen_t errorLen    = sizeof(socketError);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &errorLen) != 0 || socketError != 0) {
            return false;
        }
        ::fcntl(fd, F_SETFL, originalFlags);
        return true;
    }

    // A bare connect-and-close probe -- true if *something* is listening at
    // socketPathStr right now. Shared by TryConnectToBroker's own initial
    // attempt (inlined there, since it also needs the fd for real traffic
    // on success) and TryBecomeBrokerSpawner's double-checked-locking
    // probe below, which only ever needs a yes/no answer.
    bool CanConnect(const std::string& socketPathStr) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (socketPathStr.size() >= sizeof(addr.sun_path)) {
            ::close(fd);
            return false;
        }
        std::strncpy(addr.sun_path, socketPathStr.c_str(), sizeof(addr.sun_path) - 1);
        const bool connected = ConnectWithTimeout(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr), kConnectTimeoutMs);
        ::close(fd);
        return connected;
    }

    // lsp-broker auto-spawn follow-up. Called (from TryConnectToBroker,
    // below) the moment an initial connect attempt fails -- tries to
    // become the one `ned` process that spawns the broker daemon, so a
    // *later* attach (this buffer's own retry never happens automatically,
    // but the next buffer/language/ned-process that calls
    // TryConnectToBroker will) finds it already warm.
    //
    // Deliberately never blocks the calling thread beyond a fast
    // open+flock+fork -- this whole call chain runs synchronously on
    // LspManager::ClientForLanguage <- SyncBuffer <- BufferView::Paint(),
    // i.e. the main UI thread. Waiting here for the newly-forked daemon to
    // actually finish binding its own socket (which the original design
    // sketch called for, polling for up to ~2s) would freeze the editor
    // for that whole window on every cold start -- an unacceptable
    // regression for a feature whose entire point is startup latency. So
    // that wait (and the flock release that depends on it -- see below)
    // happens on a detached background thread instead; the caller falls
    // through to today's ordinary direct-spawn path immediately, exactly
    // as if no broker existed yet, and only benefits from the broker on
    // whichever *next* attach happens to land after the daemon has
    // actually finished starting.
    //
    // The flock on BrokerLockPath() is held from just before the
    // double-checked-locking probe through fork() *and* through the
    // background thread's own poll-until-connectable step -- not released
    // immediately after fork() returns -- because two different languages
    // (e.g. "cpp" then "prose") can each independently reach this
    // function within the same `ned` process, milliseconds apart; without
    // holding the lock until the first spawn is confirmed reachable, the
    // second call's own double-checked probe would still see nothing
    // listening and fork a *second* competing daemon (whose Run() would
    // unlink and rebind the first one's not-yet-fully-alive socket out
    // from under it). flock() releases automatically if the holding
    // process dies before releasing it, so a `ned` that crashes mid-spawn
    // never leaves a stale lock behind.
    void TryBecomeBrokerSpawner() {
        std::string           socketPathStr;
        std::filesystem::path lockPath;
        try {
            EnsureBrokerRuntimeDirectory();
            socketPathStr = BrokerSocketPath().string();
            lockPath      = BrokerLockPath();
        }
        catch (const std::exception&) {
            return;
        }

        const int lockFd = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
        if (lockFd < 0) {
            return;
        }
        if (::flock(lockFd, LOCK_EX) != 0) {
            ::close(lockFd);
            return;
        }

        if (CanConnect(socketPathStr)) {
            // Someone else already finished spawning it while we waited
            // for the lock -- nothing left to do.
            ::flock(lockFd, LOCK_UN);
            ::close(lockFd);
            return;
        }

        // Resolve our own running binary -- never a $PATH lookup by name
        // this late (POSIX-unsafe once forked, per ChildProcess.cpp's own
        // stated reasoning for why PtyProcess pre-resolves before
        // forking), and more precise than one anyway: this guarantees the
        // daemon is the exact same build currently running, not whatever
        // "ned" a stale $PATH entry might resolve to. /proc/self/exe first
        // (exact, no ambiguity); process::ResolveExecutable("ned") as a
        // fallback for the unusual case /proc isn't readable.
        char          exePathBuf[PATH_MAX];
        const ssize_t exeLen = ::readlink("/proc/self/exe", exePathBuf, sizeof(exePathBuf) - 1);
        std::string   exePath;
        if (exeLen > 0) {
            exePath.assign(exePathBuf, static_cast<std::size_t>(exeLen));
        }
        else if (const std::optional<std::string> resolved = process::ResolveExecutable("ned")) {
            exePath = *resolved;
        }
        else {
            ::flock(lockFd, LOCK_UN);
            ::close(lockFd);
            return;
        }

        const pid_t pid = ::fork();
        if (pid < 0) {
            ::flock(lockFd, LOCK_UN);
            ::close(lockFd);
            return;
        }
        if (pid == 0) {
            // Child: detach from this session (the daemon must outlive
            // whichever `ned` happened to spawn it, not be tied to its
            // controlling terminal), then exec straight into the daemon
            // entry point -- nothing else after fork() is async-signal-
            // safe in a still-multithreaded parent image, so this is
            // deliberately the only thing the child does before exec.
            // exePathBuf is filled fresh here (not reused from either
            // branch above) so the child's own argv[0] is unambiguous
            // regardless of which resolution path succeeded.
            ::setsid();
            std::strncpy(exePathBuf, exePath.c_str(), sizeof(exePathBuf) - 1);
            exePathBuf[sizeof(exePathBuf) - 1] = '\0';
            char* childArgv[] = {exePathBuf, const_cast<char*>("--lsp-broker"), nullptr};
            ::execve(exePathBuf, childArgv, environ);
            ::_exit(127); // exec failed
        }

        // Background: wait for the daemon to actually become connectable
        // (bounded budget, not indefinite), then release the lock. No
        // waitpid() here, deliberately -- unlike Link.cpp's OpenUrl, whose
        // forked children are short-lived and would accumulate zombies in
        // this same long-running `ned` process if never reaped, the
        // daemon is expected to *outlive* whichever `ned` spawned it: by
        // the time it eventually exits, this `ned` process has almost
        // always already exited too, at which point the daemon has long
        // since been reparented to init, which reaps it. The one narrow
        // edge case (the daemon crashing while this exact `ned` process
        // happens to still be running) leaves at most one harmless zombie
        // entry, cleaned up the moment this `ned` process itself exits.
        std::thread([lockFd, socketPathStr] {
            for (int attempt = 0; attempt < 40; ++attempt) { // ~2s budget, 50ms steps
                if (CanConnect(socketPathStr)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            ::flock(lockFd, LOCK_UN);
            ::close(lockFd);
        }).detach();
    }

} // namespace

std::unique_ptr<LspClient> TryConnectToBroker(const std::filesystem::path& projectRoot, const std::string& language,
                                               const std::vector<std::string>& argv, ned::ui::EventLoop& eventLoop,
                                               std::optional<std::filesystem::path> socketPathOverride) {
    std::string socketPathStr;
    try {
        socketPathStr = socketPathOverride ? socketPathOverride->string() : BrokerSocketPath().string();
    }
    catch (const std::exception&) {
        return nullptr; // couldn't even resolve a runtime directory -- see this function's own header comment
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return nullptr;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socketPathStr.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return nullptr;
    }
    std::strncpy(addr.sun_path, socketPathStr.c_str(), sizeof(addr.sun_path) - 1);

    if (!ConnectWithTimeout(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr), kConnectTimeoutMs)) {
        ::close(fd);
        // No broker reachable (or it didn't accept within kConnectTimeoutMs
        // -- see that constant's own comment) -- the common, expected outcome, especially
        // the very first time any project's LSP is used after a fresh
        // boot. Kick off (never wait for) an attempt to spawn one for next
        // time -- see TryBecomeBrokerSpawner's own doc comment for why
        // this deliberately doesn't affect how long *this* call takes.
        // Skipped entirely for an injected test socketPathOverride (real
        // production callers never pass one) -- a test double has no
        // business spawning a real daemon process.
        if (!socketPathOverride) {
            TryBecomeBrokerSpawner();
        }
        return nullptr;
    }

    const int dupFd = ::dup(fd); // PtyProcess.cpp's own precedent: ChildProcess's two fds must be independently closeable, never the same fd twice
    try {
        Transport transport(fd, dupFd, -1);

        Json argvJson = Json::array();
        for (const std::string& arg : argv) {
            argvJson.push_back(arg);
        }
        transport.WriteFrame(Json{{"jsonrpc", "2.0"},
                                   {"method", "ned/broker-attach"},
                                   {"params", Json{{"projectRoot", projectRoot.string()}, {"language", language}, {"argv", argvJson}}}}
                                  .dump());

        return std::make_unique<LspClient>(std::move(transport), eventLoop, /*startHandshakeComplete=*/false);
    }
    catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace ned::editor::lsp
