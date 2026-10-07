#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "BoundedRead.h"
#include "Editor/Process/ChildProcess.h"

using ned::editor::process::ChildProcess;
using ned::editor::process::ProcessControl;
using ned::editor::process::SpawnOptions;
using ned::editor::process::StderrMode;

namespace {

std::string ReadAll(ChildProcess& child) {
    std::string output;
    for (std::string chunk; !(chunk = child.ReadSome()).empty();) {
        output += chunk;
    }
    return output;
}

// A child "run elsewhere": the test is the other side, setting the exit.
struct FakeControl final : ProcessControl {
    struct State {
        int                exitFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        std::optional<int> exitCode;
        std::vector<int>   signals;
        bool               released = false;
        ~State() {
            ::close(exitFd);
        }
        void Exit(std::optional<int> code) {
            exitCode                = code;
            const std::uint64_t one = 1;
            (void)!::write(exitFd, &one, sizeof one);
        }
    };
    explicit FakeControl(std::shared_ptr<State> state) : state_(std::move(state)) {
    }
    int ExitFd() const noexcept override {
        return state_->exitFd;
    }
    std::optional<int> ExitCode() const noexcept override {
        return state_->exitCode;
    }
    void Signal(int signal) noexcept override {
        state_->signals.push_back(signal);
    }
    void Release() noexcept override {
        state_->released = true;
    }

    std::shared_ptr<State> state_;
};

} // namespace

TEST_CASE("ChildProcess constructor throws for an executable that can't be found on $PATH", "[Process]") {
    REQUIRE_THROWS_AS(ChildProcess({"ned-definitely-not-a-real-binary-xyz"}), std::runtime_error);
}

TEST_CASE("ChildProcess constructor throws for an empty argv", "[Process]") {
    REQUIRE_THROWS_AS(ChildProcess(std::vector<std::string>{}), std::runtime_error);
}

TEST_CASE("A child spawned later holds none of an earlier child's pipes", "[Process]") {
    ChildProcess cat({"cat"});
    ChildProcess bystander({"sleep", "30"}, StderrMode::Capture);
    cat.CloseInput();
    // cat only sees EOF, and exits, if the bystander didn't inherit its stdin.
    REQUIRE(cat.WaitReadable(std::chrono::seconds(5)));
    CHECK(cat.ReadSome().empty());
    bystander.Kill();
}

TEST_CASE("ChildProcess round-trips data through a real pipe pair with no subprocess", "[Process]") {
    int toChild[2];
    int toParent[2];
    REQUIRE(::pipe(toChild) == 0);
    REQUIRE(::pipe(toParent) == 0);

    // parent-facing wrapper: writes to toChild[1], reads from toParent[0]
    ChildProcess parent(toParent[0], toChild[1]);
    // the "child side," driven directly by the test: reads toChild[0], writes toParent[1]
    ChildProcess childSide(toChild[0], toParent[1]);

    parent.WriteAll("hello", std::chrono::milliseconds(1000));
    REQUIRE(childSide.ReadSome() == "hello");

    childSide.WriteAll("world", std::chrono::milliseconds(1000));
    REQUIRE(parent.ReadSome() == "world");
}

TEST_CASE("ChildProcess::ReadSome returns an empty string on a clean EOF", "[Process]") {
    int toChild[2];
    REQUIRE(::pipe(toChild) == 0);
    ChildProcess reader(toChild[0], -1);
    {
        ChildProcess writer(-1, toChild[1]);
        writer.WriteAll("only chunk", std::chrono::milliseconds(1000));
        REQUIRE(reader.ReadSome() == "only chunk");
        // writer goes out of scope here -- its destructor closes the write
        // end, which is what should make the next ReadSome see a clean EOF.
    }

    REQUIRE(reader.ReadSome().empty());
}

TEST_CASE("ChildProcess spawns a real process and exchanges data with it over pipes", "[Process]") {
    // Same stdbuf -o0 /bin/cat trick LspTransportTest.cpp already relies on
    // -- cat's stdout is fully buffered against a pipe, so without this the
    // test would hang waiting for output that's sitting in a buffer.
    ChildProcess child({"stdbuf", "-o0", "/bin/cat"});

    child.WriteAll("hello from a test", std::chrono::milliseconds(1000));
    REQUIRE(child.ReadSome() == "hello from a test");
}

TEST_CASE("ChildProcess::CloseInput gives the child EOF while its output stays readable", "[Process]") {
    // wc only answers once its input ends.
    ChildProcess child({"wc", "-c"});
    child.WriteAll("12345", std::chrono::milliseconds(1000));
    child.CloseInput();

    std::string output;
    for (std::string chunk = child.ReadSome(); !chunk.empty(); chunk = child.ReadSome()) {
        output += chunk;
    }
    REQUIRE(output == "5\n");
    REQUIRE(child.WaitForExit() == 0);
    // Writing after it fails rather than reaching the child.
    REQUIRE_THROWS_AS(child.WriteAll("more", std::chrono::milliseconds(100)), std::runtime_error);
}

TEST_CASE("ChildProcess::Kill terminates a real running process promptly", "[Process]") {
    ChildProcess child({"sleep", "100"});
    const pid_t  pid = child.Pid();
    REQUIRE(pid > 0);

    child.Kill();

    // The process must actually be gone -- kill(pid, 0) fails with ESRCH
    // once it's been reaped and no other process has reused the pid.
    REQUIRE(::kill(pid, 0) != 0);
}

TEST_CASE("ChildProcess::Kill also terminates a grandchild the direct child forked without exec-replacing itself",
          "[Process]") {
    // process-group-kill follow-up: a wrapper command (npx, sh -c, ...) that
    // forks a real child rather than exec-replacing itself used to survive
    // Kill() -- confirmed live against `npx claude-code-acp` forking a
    // `node` process that outlived the killed npx, leaving a reader thread
    // blocked forever on the still-open stdout pipe (the grandchild kept
    // its own inherited copy open). Reproduced here with a minimal sh
    // wrapper that backgrounds a real grandchild and prints its pid.
    ChildProcess child({"sh", "-c", "sleep 100 & echo $!; wait"});

    std::string output;
    while (output.find('\n') == std::string::npos) {
        output += child.ReadSome();
    }
    const pid_t grandchildPid = std::stoi(output);
    REQUIRE(grandchildPid > 0);
    REQUIRE(::kill(grandchildPid, 0) == 0); // alive before Kill()

    child.Kill();

    // kill(pid, 0) can still report success against a zombie -- SIGKILLed
    // but not yet reaped by its new parent after reparenting off the just-
    // killed sh -- so poll briefly rather than asserting instantaneously.
    bool gone = false;
    for (int attempt = 0; attempt < 100 && !gone; ++attempt) {
        if (::kill(grandchildPid, 0) != 0) {
            gone = true;
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    REQUIRE(gone); // the actual regression check -- previously never became true

    REQUIRE(::kill(grandchildPid, 0) != 0); // gone after Kill() -- the actual regression check
}

TEST_CASE("ChildProcess::WaitForExit reports a real exit code", "[Process]") {
    ChildProcess child({"sh", "-c", "exit 7"});

    // No output expected -- ReadSome blocks until EOF, which happens once
    // the shell exits and closes its (merged, in this default Discard mode,
    // stdout-only) fd.
    REQUIRE(child.ReadSome().empty());

    const auto exitCode = child.WaitForExit();
    REQUIRE(exitCode.has_value());
    REQUIRE(*exitCode == 7);
}

TEST_CASE("ChildProcess::WaitForExit reports nullopt after Kill", "[Process]") {
    ChildProcess child({"sleep", "100"});
    child.Kill();

    // Kill() already reaps and clears the managed pid, so WaitForExit finds
    // nothing left to wait on -- matches the documented "safe to call even
    // if Kill() was called first" contract.
    REQUIRE_FALSE(child.WaitForExit().has_value());
}

TEST_CASE("ChildProcess::ReadSome(timeout) returns nullopt when nothing arrives in time", "[Process]") {
    int toChild[2];
    REQUIRE(::pipe(toChild) == 0);
    ChildProcess reader(toChild[0], -1);
    ChildProcess writer(-1, toChild[1]); // kept alive -- no EOF, just silence

    REQUIRE_FALSE(reader.ReadSome(std::chrono::milliseconds(50)).has_value());
}

TEST_CASE("ChildProcess::ReadSome(timeout) returns data once it arrives within the window", "[Process]") {
    int toChild[2];
    REQUIRE(::pipe(toChild) == 0);
    ChildProcess reader(toChild[0], -1);
    ChildProcess writer(-1, toChild[1]);

    std::jthread delayedWriter([&writer] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        writer.WriteAll("late data", std::chrono::milliseconds(1000));
    });

    const auto chunk = reader.ReadSome(std::chrono::milliseconds(2000));
    REQUIRE(chunk.has_value());
    REQUIRE(*chunk == "late data");
}

TEST_CASE("ChildProcess::StderrFd is -1 unless constructed with StderrMode::Capture", "[Process]") {
    ChildProcess discard({"sh", "-c", "exit 0"});
    REQUIRE(discard.StderrFd() == -1);

    ChildProcess merged({"sh", "-c", "exit 0"}, StderrMode::MergeWithStdout);
    REQUIRE(merged.StderrFd() == -1);
}

// lsp-stderr-capture follow-up. Confirms Capture wires stderr onto its own
// pipe, never interleaved with stdout -- the actual risk this mode exists to
// avoid (a stray stderr byte corrupting a framed protocol's stdout stream).
TEST_CASE("StderrMode::Capture routes stderr onto its own pipe, separate from stdout", "[Process]") {
    ChildProcess child({"sh", "-c", "echo out; echo err >&2"}, StderrMode::Capture);
    REQUIRE(child.StderrFd() >= 0);

    REQUIRE(child.ReadSome() == "out\n");

    char          buffer[64] = {};
    const ssize_t n          = ned::test::BoundedRead(child.StderrFd(), buffer, sizeof(buffer));
    REQUIRE(n > 0);
    REQUIRE(std::string(buffer, static_cast<std::size_t>(n)) == "err\n");
}

TEST_CASE("ChildProcess::WaitWritable is immediately true for a fresh, empty pipe", "[Process]") {
    int toChild[2];
    REQUIRE(::pipe(toChild) == 0);
    ChildProcess writer(-1, toChild[1]);
    ChildProcess reader(toChild[0], -1); // kept alive so the write end isn't broken

    REQUIRE(writer.WaitWritable(std::chrono::milliseconds(1000)));
}

// write-side-hang-protection follow-up. Reproduces the real production
// lockup this guards against: a reader that stops draining, and a write
// large enough to fill the kernel pipe buffer (Linux's default is 64KiB)
// blocks in ::write() with nothing on the other end to unblock it --
// WriteAll must fail after timeout rather than hang the caller's thread.
TEST_CASE("ChildProcess::WriteAll throws when the reader stops draining and the pipe fills", "[Process]") {
    int toChild[2];
    REQUIRE(::pipe(toChild) == 0);
    ChildProcess reader(toChild[0], -1); // never reads -- lets the pipe buffer fill
    ChildProcess writer(-1, toChild[1]);

    const std::string payload(256 * 1024, 'x');
    REQUIRE_THROWS_AS(writer.WriteAll(payload, std::chrono::milliseconds(200)), std::runtime_error);
}

TEST_CASE("ChildProcess::WaitReadable reports EOF as readable", "[Process]") {
    int toChild[2];
    REQUIRE(::pipe(toChild) == 0);
    ChildProcess reader(toChild[0], -1);
    {
        ChildProcess writer(-1, toChild[1]);
    } // destructor closes write end -- EOF

    REQUIRE(reader.WaitReadable(std::chrono::milliseconds(2000)));
    REQUIRE(reader.ReadSome().empty());
}

TEST_CASE("ChildProcess merges stderr onto stdout when requested", "[Process]") {
    ChildProcess child({"sh", "-c", "echo out; echo err >&2"}, StderrMode::MergeWithStdout);

    std::string collected;
    while (true) {
        const std::string chunk = child.ReadSome();
        if (chunk.empty()) {
            break;
        }
        collected += chunk;
    }

    // Shell buffering makes strict interleaving order non-deterministic --
    // assert both lines arrived, not their relative order.
    REQUIRE(collected.find("out") != std::string::npos);
    REQUIRE(collected.find("err") != std::string::npos);
}

// broker-reader-deadlock follow-up.
TEST_CASE("ChildProcess::CloseConnection unblocks a concurrent reader without destroying the object", "[Process]") {
    // The whole point of this method: the LSP broker has to wake a thread
    // parked in a blocking read from *another* thread. It used to do that by
    // destroying the ChildProcess out from under the reader, which woke it
    // and then returned it into freed memory.
    ChildProcess child({"cat"}); // reads stdin forever, writes nothing -- the reader below genuinely blocks

    std::atomic<bool> readerReturned{false};
    std::thread       reader([&child, &readerReturned] {
        (void)child.ReadSome(); // blocks until the connection is closed
        readerReturned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // let the reader reach its blocking read
    REQUIRE_FALSE(readerReturned.load());

    child.CloseConnection();
    reader.join(); // returns only because CloseConnection woke it
    REQUIRE(readerReturned.load());

    // Still a live object with valid state -- not a husk the caller has to
    // avoid touching -- and its destructor still reaps the child.
    REQUIRE(child.ReadSome().empty());
    REQUIRE_THROWS_AS(child.WriteAll("late", std::chrono::milliseconds(1000)), std::runtime_error);
}

TEST_CASE("ChildProcess::CloseConnection is idempotent and safe before the destructor", "[Process]") {
    ChildProcess child({"cat"});
    child.CloseConnection();
    child.CloseConnection(); // must not double-close a recycled fd number
    REQUIRE_FALSE(child.WaitReadable(std::chrono::milliseconds(0)));
}

namespace {

// Polls flag for up to limit -- a test that would otherwise hang fails instead.
bool BecomesTrueWithin(const std::atomic<bool>& flag, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!flag.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return flag.load();
}

} // namespace

// The child need not cooperate: sleep never reads stdin and holds its
// stdout open, so only the connection itself can wake the reader.
TEST_CASE("ChildProcess::CloseConnection wakes a reader while the child keeps its output open", "[Process]") {
    ChildProcess child({"sleep", "100"});

    std::atomic<bool> readerReturned{false};
    std::thread       reader([&child, &readerReturned] {
        (void)child.ReadSome();
        readerReturned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    child.CloseConnection();
    const bool woke = BecomesTrueWithin(readerReturned, std::chrono::seconds(2));
    if (!woke) {
        child.Kill(); // unblocks the reader so the failure is reported rather than hung
    }
    reader.join();
    REQUIRE(woke);
}

TEST_CASE("ChildProcess::WaitForExit returns once the connection is closed even though the child is still running", "[Process]") {
    ChildProcess child({"sleep", "100"});

    std::atomic<bool>  waiterReturned{false};
    std::optional<int> exitCode = 0;
    std::thread        waiter([&] {
        exitCode       = child.WaitForExit();
        waiterReturned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    child.CloseConnection();
    const bool woke = BecomesTrueWithin(waiterReturned, std::chrono::seconds(2));
    if (!woke) {
        child.Kill();
    }
    waiter.join();
    REQUIRE(woke);
    CHECK_FALSE(exitCode.has_value());
}

// The child is reaped exactly once, by whichever thread gets there first;
// the other learns the outcome instead of waiting on a pid that is gone.
TEST_CASE("ChildProcess::Kill returns promptly while another thread waits for the exit", "[Process]") {
    ChildProcess child({"sleep", "100"});

    std::optional<int> exitCode = 0;
    std::thread        waiter([&] { exitCode = child.WaitForExit(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the waiter is parked first

    const auto start = std::chrono::steady_clock::now();
    child.Kill();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    waiter.join();

    CHECK(elapsed < std::chrono::seconds(1));
    CHECK_FALSE(exitCode.has_value()); // killed by a signal, not a clean exit
}

// closed-connection-never-parks follow-up. The deterministic regression test
// for the flaky protocol-client `ctest -j8` timeouts: poll(2) *ignores* a
// negative fd rather than failing on it, so a one-entry pollfd set holding
// only -1 waits out its full timeout -- and the unbounded (negative-timeout)
// first-byte wait every framing reader uses parks forever. A protocol
// client's read thread that hadn't been scheduled into its first read by the
// time its owner was destroyed landed exactly here, wedging the destructor's
// own join() behind it.
TEST_CASE("ChildProcess::WaitReadable returns immediately on a closed connection instead of parking", "[Process]") {
    ChildProcess child({"cat"});
    child.CloseConnection();

    const auto start = std::chrono::steady_clock::now();
    REQUIRE_FALSE(child.WaitReadable(std::chrono::milliseconds(-1))); // poll(2)'s "block forever" sentinel
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::seconds(1));
}

TEST_CASE("ChildProcess::WaitWritable returns immediately on a closed connection instead of parking", "[Process]") {
    ChildProcess child({"cat"});
    child.CloseConnection();

    const auto start = std::chrono::steady_clock::now();
    REQUIRE_FALSE(child.WaitWritable(std::chrono::milliseconds(-1)));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::seconds(1));
}

// The read-side half of the same fix: a framing reader's unbounded
// first-byte wait against an already-closed connection must report a clean
// disconnect (nullopt/EOF), not a "stalled mid-message" error and not a hang.
TEST_CASE("ChildProcess::ReadSome returns EOF rather than parking once the connection is closed", "[Process]") {
    ChildProcess child({"cat"});
    child.CloseConnection();

    const auto start = std::chrono::steady_clock::now();
    REQUIRE(child.ReadSome().empty()); // empty == EOF
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
}

TEST_CASE("ChildProcess starts a child in the given directory with the given environment", "[Process]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    ::setenv("NED_CHILD_TEST_REMOVED", "still here", 1);
    ChildProcess child({"sh", "-c", "pwd; echo \"$NED_CHILD_TEST_SET\"; echo \"[${NED_CHILD_TEST_REMOVED-gone}]\""},
                       SpawnOptions{.stderrMode = StderrMode::Discard,
                                    .cwd        = dir,
                                    .env        = {{"NED_CHILD_TEST_SET", "set"}, {"NED_CHILD_TEST_REMOVED", std::nullopt}}});
    CHECK(ReadAll(child) == std::filesystem::canonical(dir).string() + "\nset\n[gone]\n");
    CHECK(child.WaitForExit() == 0);
    ::unsetenv("NED_CHILD_TEST_REMOVED");
}

TEST_CASE("ChildProcess refuses to start in a directory that doesn't exist", "[Process]") {
    CHECK_THROWS_AS(ChildProcess({"true"}, SpawnOptions{.stderrMode = StderrMode::Discard, .cwd = "/nonexistent/ned/dir", .env = {}}),
                    std::runtime_error);
}

TEST_CASE("ChildProcess::Signal reaches the child's process group", "[Process]") {
    ChildProcess child({"sh", "-c", "trap 'echo term; exit 3' TERM; echo ready; while :; do sleep 0.05; done"});
    REQUIRE(child.ReadSome(std::chrono::seconds(10)) == std::optional<std::string>("ready\n"));
    child.Signal(SIGTERM);
    CHECK(ReadAll(child) == "term\n");
    CHECK(child.WaitForExit() == 3);
}

TEST_CASE("A ChildProcess backed by a ProcessControl signals through it and waits on its exit", "[Process]") {
    int ours[2]   = {-1, -1};
    int theirs[2] = {-1, -1};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ours) == 0);
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, theirs) == 0);
    auto state = std::make_shared<FakeControl::State>();
    {
        ChildProcess child(ours[0], ::dup(ours[0]), theirs[0], std::make_unique<FakeControl>(state));
        CHECK(child.Pid() == -1);

        child.WriteAll("in", std::chrono::seconds(5));
        char buffer[8] = {};
        REQUIRE(ned::test::TryBoundedRead(ours[1], buffer, sizeof buffer, std::chrono::seconds(5)) == 2);
        CHECK(std::string(buffer, 2) == "in");
        REQUIRE(::write(ours[1], "out", 3) == 3);
        CHECK(child.ReadSome() == "out");
        REQUIRE(::write(theirs[1], "err", 3) == 3);
        CHECK(child.ReadSomeStderr() == "err");

        // Kill only signals: the exit is the other side's to report.
        child.Kill();
        child.Signal(SIGINT);
        CHECK(state->signals == std::vector<int>{SIGKILL, SIGINT});

        std::jthread exits([&state] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            state->Exit(9);
        });
        CHECK(child.WaitForExit() == 9);
        CHECK(child.WaitForExit() == 9);
        child.Signal(SIGTERM); // reaped: nothing more is sent
        CHECK(state->signals.size() == 2);
        CHECK_FALSE(state->released);
    }
    CHECK(state->released);
    ::close(ours[1]);
    ::close(theirs[1]);
}
