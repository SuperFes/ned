#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "Editor/Lsp/LspBrokerDaemon.h"
#include "Editor/Lsp/Transport.h"

// broker-reader-deadlock follow-up. A live regression harness for the
// daemon's own threading/lifetime paths -- the half of the broker that had
// no test coverage at all until it deadlocked in production (2026-09-07:
// an idle sweep that closed an entry joined that entry's reader thread
// while holding the mutex the reader needed to finish, wedging the whole
// daemon -- no more accepts, no idle timeout, no executable-change check --
// until it was killed by hand).
//
// Everything here drives the real BrokerDaemon over a real AF_UNIX socket
// with real subprocesses, just with second-scale timeouts instead of the
// production minute-scale ones -- which is also what finally puts the
// daemon's threading paths inside the ASan/UBSan build's coverage at all.
//
// Worth knowing about the other half of that fix (a Transport destroyed out
// from under a thread parked inside its ReadFrame, now prevented by
// shared_ptr ownership plus Transport::Close): reintroducing the old shape
// under ASan here did *not* reliably report it. The reader wakes the
// instant the fd closes, which is inside the Transport destructor's own
// child-reap window, so it usually touches the object before the free
// actually completes -- the same reason the bug looked benign in production
// for so long. The ownership change removes it by construction rather than
// leaving it to a sanitizer that only catches it when the race lands the
// other way.

using ned::editor::lsp::BrokerDaemon;
using ned::editor::lsp::BrokerDaemonOptions;
using ned::editor::lsp::Json;
using ned::editor::lsp::Transport;

namespace {

// Short enough to stay well under sockaddr_un's sun_path limit --
// LspBrokerConnectTest.cpp's own UniqueSocketPath reasoning, plus a
// per-test suffix so two TEST_CASEs never collide.
std::filesystem::path SocketPathFor(const std::string& suffix) {
    return std::filesystem::path("/tmp") / ("ned-brokerd-test-" + std::to_string(::getpid()) + "-" + suffix + ".sock");
}

BrokerDaemonOptions TestOptions(const std::filesystem::path& socketPath) {
    return BrokerDaemonOptions{
        .maxConcurrentServers = 4,
        // Fast enough to keep a test to a few seconds, slow enough that
        // the sweep isn't spinning on the mutex the whole time.
        .idleSweepInterval      = std::chrono::milliseconds(100),
        .perEntryIdleTimeout    = std::chrono::milliseconds(300),
        .wholeDaemonIdleTimeout = std::chrono::seconds(1),
        .socketPath             = socketPath,
        // The test binary is itself a build artifact that may be
        // rebuilt while a test runs; this check has nothing to do with
        // what's under test here.
        .watchExecutableIdentity = false,
    };
}

// Runs one daemon on its own thread and reports whether Run() ever
// returned. On the failure path the thread is detached rather than
// joined -- a regression here means the daemon is wedged forever, and
// joining it would hang the whole test binary instead of failing one
// TEST_CASE. The daemon is kept alive by the shared_ptr the thread
// captured, so a detached thread never touches a destroyed object.
struct DaemonHarness {
    std::shared_ptr<BrokerDaemon> daemon;
    std::atomic<bool>             finished{false};
    std::atomic<int>              exitCode{-1};
    std::thread                   thread;
    std::filesystem::path         socketPath;
    std::stringstream             capturedLog;
    std::streambuf*               previousCerr = nullptr;

    explicit DaemonHarness(const std::string& suffix) : socketPath(SocketPathFor(suffix)) {
        ::unlink(socketPath.c_str());
        // The daemon logs to stderr by design; capture it so the test
        // output stays readable *and* so the assertions below can be
        // about what the daemon actually did, not just that it exited.
        previousCerr = std::cerr.rdbuf(capturedLog.rdbuf());
        daemon       = std::make_shared<BrokerDaemon>(TestOptions(socketPath));
        thread       = std::thread([this, held = daemon] {
            exitCode = held->Run();
            finished = true;
        });
        WaitForListener();
    }

    ~DaemonHarness() {
        if (finished.load() && thread.joinable()) {
            thread.join();
            daemon.reset(); // destroys the daemon (joining any leftover reader threads) while cerr is still captured
        }
        else if (thread.joinable()) {
            thread.detach(); // see this struct's own doc comment; the detached thread's own copy keeps the daemon alive
        }
        if (previousCerr != nullptr) {
            std::cerr.rdbuf(previousCerr);
        }
        ::unlink(socketPath.c_str());
    }

    DaemonHarness(const DaemonHarness&)            = delete;
    DaemonHarness& operator=(const DaemonHarness&) = delete;

    // The socket appears a moment after Run() starts; every test needs
    // it before it can connect.
    void WaitForListener() {
        for (int i = 0; i < 200 && !std::filesystem::exists(socketPath); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    [[nodiscard]] bool WaitForExit(std::chrono::milliseconds limit) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (finished.load()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }

    [[nodiscard]] std::string Log() {
        return capturedLog.str();
    }
};

// One client connection to the daemon, closed on scope exit. Deliberately
// raw (a socket plus a Transport) rather than TryConnectToBroker, which
// would drag an EventLoop/LspClient in for no benefit here.
struct ClientConnection {
    int                        fd = -1;
    std::unique_ptr<Transport> transport;

    explicit ClientConnection(const std::filesystem::path& socketPath) {
        fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
        REQUIRE(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        const int dupFd = ::dup(fd);
        transport       = std::make_unique<Transport>(fd, dupFd, -1);
    }

    ClientConnection(const ClientConnection&)            = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;

    // "cat" stands in for a language server: a real subprocess on a real
    // pipe pair that never volunteers a frame, so the daemon's reader
    // thread for it is genuinely parked inside ReadFrame() when the idle
    // sweep closes it -- which is exactly the state the deadlock needed.
    void Attach(const std::string& root, const std::string& language) {
        const Json frame = {
            {"jsonrpc", "2.0"},
            {"method", "ned/broker-attach"},
            {"params", {{"projectRoot", root}, {"language", language}, {"argv", Json::array({"cat"})}}},
        };
        transport->WriteFrame(frame.dump());
    }

    void RequestShutdown() {
        const Json frame = {{"jsonrpc", "2.0"}, {"method", "ned/broker-shutdown"}};
        transport->WriteFrame(frame.dump());
    }

    void Close() {
        transport.reset(); // closes both fds -- the daemon sees EOF on this connection
    }
};

bool LogContains(const std::string& log, const std::string& needle) {
    return log.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("The broker daemon still exits on its own after an idle sweep tears down a spawned server", "[LspBrokerDaemon]") {
    // The exact production freeze: a client attaches (spawning a real
    // subprocess whose reader thread then blocks in ReadFrame), the client
    // goes away, and the per-entry idle sweep tears the entry down. Before
    // the fix the sweep joined that reader thread while holding mutex_ and
    // the daemon wedged permanently -- no further sweeps, so the
    // whole-daemon idle timeout below never fired and Run() never returned.
    DaemonHarness harness("idle-sweep");
    {
        ClientConnection client(harness.socketPath);
        client.Attach("/tmp/ned-brokerd-test-root", "cpp");
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // let the spawn + reader thread settle
        client.Close();
    }

    REQUIRE(harness.WaitForExit(std::chrono::seconds(20)));
    REQUIRE(harness.exitCode.load() == 0);

    const std::string log = harness.Log();
    INFO(log);
    REQUIRE(LogContains(log, "spawned root=/tmp/ned-brokerd-test-root language=cpp"));
    REQUIRE(LogContains(log, "idle-timeout sweep closed"));         // the sweep really did tear an entry down...
    REQUIRE(LogContains(log, "whole-daemon idle timeout reached")); // ...and the sweep thread was still alive afterwards
}

TEST_CASE("The broker daemon survives repeated attach/spawn/idle-teardown cycles", "[LspBrokerDaemon]") {
    // The deadlock was a race (it lost about 40% of the time in production),
    // so one cycle is a weak probe. Three consecutive cycles in one daemon
    // lifetime also prove the reaper keeps up: a reader handle that is never
    // reaped would accumulate, and a reaper that joined an unfinished thread
    // would wedge on any one of them.
    DaemonHarness harness("cycles");
    for (int cycle = 0; cycle < 3; ++cycle) {
        ClientConnection client(harness.socketPath);
        client.Attach("/tmp/ned-brokerd-test-root-" + std::to_string(cycle), "cpp");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        client.Close();
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // let the per-entry sweep fire between cycles
    }

    REQUIRE(harness.WaitForExit(std::chrono::seconds(20)));
    REQUIRE(harness.exitCode.load() == 0);

    const std::string log = harness.Log();
    INFO(log);
    REQUIRE(LogContains(log, "root=/tmp/ned-brokerd-test-root-2")); // the third cycle really did get served
    REQUIRE(LogContains(log, "whole-daemon idle timeout reached"));
}

TEST_CASE("The broker daemon shuts down promptly on a ned/broker-shutdown control frame", "[LspBrokerDaemon]") {
    // Guards the extraction of BrokerDaemon out of LspBrokerMain.cpp: the
    // control-connection path (never attached, so it has no entry to erase)
    // still ends the process, and does it well inside the whole-daemon idle
    // timeout that would otherwise take over.
    DaemonHarness harness("shutdown");
    {
        ClientConnection attached(harness.socketPath);
        attached.Attach("/tmp/ned-brokerd-test-root", "cpp");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        ClientConnection control(harness.socketPath);
        control.RequestShutdown();

        REQUIRE(harness.WaitForExit(std::chrono::seconds(20)));
    }
    REQUIRE(harness.exitCode.load() == 0);

    const std::string log = harness.Log();
    INFO(log);
    REQUIRE(LogContains(log, "received ned/broker-shutdown"));
    REQUIRE(LogContains(log, "daemon exiting"));
}
