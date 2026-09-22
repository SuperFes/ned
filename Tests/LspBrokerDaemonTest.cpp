#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
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

#include "Editor/Lsp/BrokerDaemon.h"
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

BrokerDaemonOptions TestOptions(const std::filesystem::path& socketPath,
                                std::chrono::milliseconds     wholeDaemonIdleTimeout = std::chrono::seconds(1)) {
    return BrokerDaemonOptions{
        .maxConcurrentServers = 4,
        // Fast enough to keep a test to a few seconds, slow enough that
        // the sweep isn't spinning on the mutex the whole time.
        .idleSweepInterval      = std::chrono::milliseconds(100),
        .perEntryIdleTimeout    = std::chrono::milliseconds(300),
        .wholeDaemonIdleTimeout = wholeDaemonIdleTimeout,
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
    // broker-info follow-up: what the daemon thread writes lives behind a
    // shared_ptr rather than in this struct. The destructor below detaches
    // that thread when the daemon hasn't stopped yet, and a detached thread
    // outliving the TEST_CASE would otherwise write `finished`/`exitCode`
    // into a harness whose stack frame is gone -- a stack-use-after-return
    // ASan reports the moment any test leaves a daemon running at scope
    // exit (the `held = daemon` capture only ever kept the *daemon* alive,
    // never this). Both ends now own the state they touch.
    struct RunState {
        std::atomic<bool> finished{false};
        std::atomic<int>  exitCode{-1};
    };

    std::shared_ptr<BrokerDaemon> daemon;
    std::shared_ptr<RunState>     state = std::make_shared<RunState>();
    std::thread                   thread;
    std::filesystem::path         socketPath;
    std::stringstream             capturedLog;
    std::streambuf*               previousCerr = nullptr;

    explicit DaemonHarness(const std::string& suffix, std::chrono::milliseconds wholeDaemonIdleTimeout = std::chrono::seconds(1))
        : socketPath(SocketPathFor(suffix)) {
        ::unlink(socketPath.c_str());
        // The daemon logs to stderr by design; capture it so the test
        // output stays readable *and* so the assertions below can be
        // about what the daemon actually did, not just that it exited.
        previousCerr = std::cerr.rdbuf(capturedLog.rdbuf());
        daemon       = std::make_shared<BrokerDaemon>(TestOptions(socketPath, wholeDaemonIdleTimeout));
        thread       = std::thread([held = daemon, state = state] {
            state->exitCode = held->Run();
            state->finished = true;
        });
        WaitForListener();
    }

    ~DaemonHarness() {
        if (state->finished.load() && thread.joinable()) {
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
            if (state->finished.load()) {
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
// would drag an EventLoop/Client in for no benefit here.
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

TEST_CASE("The broker daemon still exits on its own after an idle sweep tears down a spawned server", "[BrokerDaemon]") {
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
    REQUIRE(harness.state->exitCode.load() == 0);

    const std::string log = harness.Log();
    INFO(log);
    REQUIRE(LogContains(log, "spawned root=/tmp/ned-brokerd-test-root language=cpp"));
    REQUIRE(LogContains(log, "idle-timeout sweep closed"));         // the sweep really did tear an entry down...
    REQUIRE(LogContains(log, "whole-daemon idle timeout reached")); // ...and the sweep thread was still alive afterwards
}

TEST_CASE("The broker daemon survives repeated attach/spawn/idle-teardown cycles", "[BrokerDaemon]") {
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
    REQUIRE(harness.state->exitCode.load() == 0);

    const std::string log = harness.Log();
    INFO(log);
    REQUIRE(LogContains(log, "root=/tmp/ned-brokerd-test-root-2")); // the third cycle really did get served
    REQUIRE(LogContains(log, "whole-daemon idle timeout reached"));
}

TEST_CASE("The broker daemon shuts down promptly on a ned/broker-shutdown control frame", "[BrokerDaemon]") {
    // Guards the extraction of BrokerDaemon out of BrokerMain.cpp: the
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
    REQUIRE(harness.state->exitCode.load() == 0);

    const std::string log = harness.Log();
    INFO(log);
    REQUIRE(LogContains(log, "received ned/broker-shutdown"));
    REQUIRE(LogContains(log, "daemon exiting"));
}

TEST_CASE("A whole-daemon idle timeout of zero never fires", "[BrokerDaemon]") {
    // foreground-mode follow-up: --foreground passes wholeDaemonIdleTimeout
    // = 0 for a deliberately always-on instance. Confirms that's really
    // "never," not just "a very long timeout" -- the daemon sits with zero
    // connections for several multiples of what would otherwise be an
    // immediate exit (the harness's own default is 1 second; this waits 10x
    // that) and must still be running, then a control-frame shutdown is
    // used to end the test cleanly.
    DaemonHarness harness("never-idle", std::chrono::milliseconds::zero());
    REQUIRE_FALSE(harness.WaitForExit(std::chrono::seconds(10)));

    {
        ClientConnection control(harness.socketPath);
        control.RequestShutdown();
        REQUIRE(harness.WaitForExit(std::chrono::seconds(20)));
    }
    REQUIRE(harness.state->exitCode.load() == 0);

    const std::string log = harness.Log();
    INFO(log);
    REQUIRE_FALSE(LogContains(log, "whole-daemon idle timeout reached"));
    REQUIRE(LogContains(log, "received ned/broker-shutdown"));
}

TEST_CASE("The broker daemon shuts down promptly on SIGTERM, the same way as a control-frame shutdown", "[BrokerDaemon]") {
    // foreground-mode follow-up: systemd's own stop signal (and Ctrl-C
    // under a manually-run --foreground/--lsp-broker) must run the exact
    // same graceful router_.Shutdown() sequence the ned/broker-shutdown
    // control frame already does, not just terminate the process outright.
    // wholeDaemonIdleTimeout is disabled here so only the signal can end
    // the run -- proves the signal path on its own, not a race with the
    // idle timeout.
    DaemonHarness harness("sigterm", std::chrono::milliseconds::zero());
    {
        ClientConnection client(harness.socketPath);
        client.Attach("/tmp/ned-brokerd-test-root", "cpp");
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // let the spawn settle

        // The client is still attached when the signal lands -- proves
        // router_.Shutdown() really does tear down a *live* entry (real
        // LSP shutdown/exit, close the client) rather than only handling
        // the already-idle case.
        REQUIRE(::kill(::getpid(), SIGTERM) == 0);
        REQUIRE(harness.WaitForExit(std::chrono::seconds(20)));
    }
    REQUIRE(harness.state->exitCode.load() == 0);

    const std::string log = harness.Log();
    INFO(log);
    REQUIRE(LogContains(log, "received shutdown signal"));
    REQUIRE(LogContains(log, "daemon exiting"));
}

// foreground-takeover follow-up: the daemon's own half -- answering "what
// are you?" so a second `ned --foreground` can tell an ephemeral broker it
// may replace from a supervised one it must not touch.
TEST_CASE("The daemon answers ned/broker-info with its pid and its ephemeral mode", "[BrokerDaemon]") {
    DaemonHarness    harness("info-ephemeral", std::chrono::seconds(5));
    ClientConnection client(harness.socketPath);

    client.transport->WriteFrame(Json{{"jsonrpc", "2.0"}, {"id", 7}, {"method", "ned/broker-info"}}.dump());
    const std::optional<std::string> frame = client.transport->ReadFrame(std::chrono::seconds(2));
    REQUIRE(frame.has_value());
    const Json answer = Json::parse(*frame);
    CHECK(answer["id"] == 7);
    CHECK(answer["result"]["pid"] == static_cast<int>(::getpid())); // the test daemon runs in this process
    CHECK(answer["result"]["supervised"] == false);

    // See the unknown-control-frame case below: stopped deliberately rather
    // than left running past the end of the TEST_CASE.
    ClientConnection stopper(harness.socketPath);
    stopper.RequestShutdown();
    CHECK(harness.WaitForExit(std::chrono::seconds(5)));
}

TEST_CASE("A supervised daemon says so", "[BrokerDaemon]") {
    const std::filesystem::path socketPath = SocketPathFor("info-supervised");
    ::unlink(socketPath.c_str());
    BrokerDaemonOptions options = TestOptions(socketPath, std::chrono::seconds(5));
    options.supervised          = true;

    auto        daemon = std::make_shared<BrokerDaemon>(options);
    std::thread thread([held = daemon] { (void)held->Run(); });
    for (int i = 0; i < 200 && !std::filesystem::exists(socketPath); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    {
        ClientConnection client(socketPath);
        client.transport->WriteFrame(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ned/broker-info"}}.dump());
        const std::optional<std::string> frame = client.transport->ReadFrame(std::chrono::seconds(2));
        REQUIRE(frame.has_value());
        CHECK(Json::parse(*frame)["result"]["supervised"] == true);
    }
    {
        ClientConnection stopper(socketPath);
        stopper.RequestShutdown();
    }
    thread.join();
    ::unlink(socketPath.c_str());
}

TEST_CASE("An unknown control frame still drops the connection", "[BrokerDaemon]") {
    // The guard that makes BrokerProbe::State::Unidentified a real state
    // rather than a theoretical one: anything that isn't attach/shutdown/
    // info gets nothing back, which is exactly how a daemon built before
    // ned/broker-info behaves.
    DaemonHarness    harness("info-unknown", std::chrono::seconds(5));
    ClientConnection client(harness.socketPath);

    client.transport->WriteFrame(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ned/not-a-control-message"}}.dump());
    CHECK_FALSE(client.transport->ReadFrame(std::chrono::seconds(2)).has_value()); // EOF, not an answer

    // Stopped here rather than left to the idle timeout: a daemon still
    // running when the harness goes out of scope is exactly what the
    // detached-thread path above exists for, and leaving one behind in
    // every run makes that path routine instead of exceptional.
    ClientConnection stopper(harness.socketPath);
    stopper.RequestShutdown();
    CHECK(harness.WaitForExit(std::chrono::seconds(5)));
}
