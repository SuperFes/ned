//
// lsp-broker follow-up. The imperative I/O shell around Broker.h's pure
// BrokerRouter -- real sockets, real subprocesses, real threads.
// BrokerMain.h's RunLspBrokerDaemon() is a thin wrapper that constructs
// one of these with production defaults and runs it.
//
// broker-reader-deadlock follow-up: this class used to live in an anonymous
// namespace inside BrokerMain.cpp, which put the daemon's whole
// threading/lifetime story -- the part that actually deadlocked in
// production -- outside every test binary, and so outside the ASan/UBSan
// build's coverage too. It's a real declared type now, with its timings and
// socket path injectable (production defaults unchanged), specifically so
// Tests/BrokerDaemonTest.cpp can drive the real accept/spawn/sweep/reap
// paths against a private socket with second-scale timeouts.
//

#ifndef NED_EDITOR_LSP_BROKERDAEMON_H
#define NED_EDITOR_LSP_BROKERDAEMON_H

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/stat.h>

#include "Broker.h"
#include "Transport.h"

namespace ned::editor::lsp {

// Every knob the daemon reads at startup. Defaults are exactly the
// production values; a test overrides the timings (to seconds) and the
// socket path (to its own private one, so a running real daemon is never
// disturbed).
struct BrokerDaemonOptions {
    int maxConcurrentServers = 8;

    // How often the idle sweep runs. Also the granularity of both timeouts
    // below and of the executable-staleness check.
    std::chrono::milliseconds idleSweepInterval = std::chrono::seconds(10);

    // How long one (root, language) entry may sit with no attached client
    // before it's torn down.
    std::chrono::milliseconds perEntryIdleTimeout = std::chrono::minutes(30);

    // How long the whole daemon may sit with no connections at all before
    // it exits -- see BrokerMain.cpp's own historical note on why this is
    // about a minute rather than hours.
    std::chrono::milliseconds wholeDaemonIdleTimeout = std::chrono::minutes(1);

    // Empty means BrokerSocketPath() (and the matching
    // EnsureBrokerRuntimeDirectory()); a test passes its own short path.
    std::filesystem::path socketPath;

    // Whether to self-shut-down when this process's own executable is
    // replaced on disk. Meaningless (and a nuisance) for a test daemon
    // running out of a build tree that may be rebuilt underneath it.
    bool watchExecutableIdentity = true;
};

// Owns every live connection (client sockets and real language-server
// subprocesses) and the one BrokerRouter they're all relayed through.
//
// Threading: one jthread per connection, each blocking on its own
// Transport::ReadFrame() -- Client.h's own established shape, minus
// EventLoop::Post (there is none here; a plain mutex_ guards router_ and
// every map below instead). Every router_ call is made while holding
// mutex_; WriteFrame calls are made *outside* it (a shared_ptr to the
// transport is taken under a short-held lock, then used after releasing it)
// so one slow/wedged peer's blocking write can't stall every other
// connection's routing.
//
// Lifetime/erase discipline (broker-reader-deadlock follow-up -- this is
// the part that used to be wrong, twice over):
//
//   * Transports are held by shared_ptr, and a reader thread keeps its own
//     strong reference for the whole duration of a ReadFrame() call. A
//     CloseClient/CloseServer action from another thread erases the map
//     entry and calls Transport::Close() on it -- closing the connection is
//     what wakes the blocked reader, exactly as before, but the object
//     itself now outlives that wake-up and is destroyed by whichever
//     reference goes away last. Destroying the Transport out from under a
//     thread parked inside one of its member functions (the previous
//     unique_ptr-plus-raw-pointer shape) was a use-after-free that happened
//     to work because the fd close usually won the race.
//
//   * A reader thread's own jthread handle is never erased by that same
//     thread (a thread cannot join itself). Instead each reader announces
//     itself in finishedClientReaders_/finishedServerReaders_ as its very
//     last act, and ReapFinishedThreads() -- called from the idle-sweep
//     tick -- joins *only* announced threads, and does the joining after
//     releasing mutex_. Both halves matter: reaping only announced threads
//     means a join can never wait on work that hasn't happened yet, and
//     joining outside the lock means it can never wait on work that needs
//     the lock. The old code did neither, so an idle sweep that closed an
//     entry would join that entry's reader thread while holding the very
//     mutex the reader needed to finish -- a hard deadlock that wedged the
//     whole daemon (no more accepts, no idle timeout, no executable-change
//     check) until it was killed. Reproduced and fixed 2026-09-07; see
//     Tests/BrokerDaemonTest.cpp.
//
// A genuinely hung language server that ignores "exit" and never closes its
// pipes still leaves one reader thread parked in ReadFrame() forever. That
// thread simply never announces itself, so its handle is never reaped and
// the sweep is never blocked by it -- the process exit reclaims it. Known,
// accepted, and now harmless to everything else.
class BrokerDaemon {
  public:
    explicit BrokerDaemon(BrokerDaemonOptions options);
    explicit BrokerDaemon(int maxConcurrentServers);

    // Closes every remaining connection and joins every reader thread
    // before any other member is destroyed -- see the member declarations
    // below on why leaving that to member destruction order alone is not
    // enough.
    ~BrokerDaemon();

    BrokerDaemon(const BrokerDaemon&)            = delete;
    BrokerDaemon& operator=(const BrokerDaemon&) = delete;

    // Binds the socket, accepts connections, and relays until a
    // ned/broker-shutdown control message or one of the idle timeouts ends
    // it. Returns a process exit code: 0 on a clean shutdown, nonzero on a
    // fatal bind/listen failure (reported to stderr before returning).
    [[nodiscard]] int Run();

  private:
    using TransportPtr = std::shared_ptr<lsp::Transport>;

    struct ExecutableIdentity {
        dev_t  device     = 0;
        ino_t  inode      = 0;
        time_t modifiedAt = 0;

        bool operator==(const ExecutableIdentity&) const = default;
    };

    void ApplyActions(std::vector<BrokerAction> actions);
    void ApplySpawnServer(const BrokerAction& action);
    void ApplySendToServer(const BrokerAction& action);
    void ApplySendToClient(const BrokerAction& action);
    void CloseTransport(TransportPtr transport);
    void HandleClientConnection(ConnectionId connId, int fd);
    void ClientReaderFinished(ConnectionId connId);
    void ServerReadLoop(const std::string& root, const std::string& language);
    void ServerReaderFinished(const std::string& key);
    void ReapFinishedThreads();
    void IdleSweepLoop(std::stop_token stopToken);

    BrokerDaemonOptions options_;

    std::mutex                                     mutex_; // guards router_ and every map below
    BrokerRouter                                   router_;
    std::unordered_map<ConnectionId, TransportPtr> clientTransports_;
    std::unordered_map<std::string, TransportPtr>  serverTransports_;
    // broker-reader-deadlock follow-up: a reader announces itself here as
    // its last act, and only announced handles are ever joined -- see this
    // class's own lifetime/erase-discipline comment above.
    std::unordered_set<ConnectionId>     finishedClientReaders_;
    std::unordered_set<std::string>      finishedServerReaders_;
    std::atomic<ConnectionId>            nextConnectionId_{1};
    int                                  listenFd_ = -1;
    std::atomic<bool>                    shuttingDown_{false};
    std::optional<std::filesystem::path> exePath_;              // stale-broker-shutdown follow-up
    std::optional<ExecutableIdentity>    exeIdentityAtStartup_; // ditto -- only meaningful if exePath_ is set

    // Declared last, so destroyed first: destroying a jthread joins it, and
    // a reader thread being joined is still touching the maps/sets above on
    // its way out (the transport erase, the router callback, its own
    // finished-reader announcement). Any member declared *after* these
    // would already be gone by the time that thread ran its last lines --
    // a real segfault, caught by Tests/BrokerDaemonTest.cpp the first
    // time these threads outlived a daemon's own destruction rather than
    // Run()'s return. ~BrokerDaemon() additionally closes every remaining
    // connection first, so those joins can't block on a peer that would
    // otherwise never send anything again.
    std::unordered_map<ConnectionId, std::jthread> clientReaderThreads_;
    std::unordered_map<std::string, std::jthread>  serverReaderThreads_;
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_BROKERDAEMON_H
