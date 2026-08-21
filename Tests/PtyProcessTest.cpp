#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/ioctl.h>

#include "Editor/Terminal/PtyProcess.h"
#include "UI/EventLoop.h"

using ned::editor::terminal::PtyProcess;

// Same fixture rationale as TaskProcessTest.cpp: a real ned::ui::EventLoop
// is constructed but Run() never starts; the Dispatch* seams are driven
// directly, and the round-trip test below drains the Post queue by hand via
// DrainPosted_. One pty-specific caveat inherited from LspClientTest.cpp's
// documented EOF gotcha, in a worse form: a pty master never reports EOF
// while any slave fd is open, so the only thing that ever unblocks the
// background read loop is the child actually dying -- every fixture here
// relies on ChildProcess's destructor teardown (close master -> SIGHUP ->
// grace -> SIGKILL) for that, which is exactly the production shutdown path.

TEST_CASE("PtyProcess::DispatchOutput invokes onOutput with the given chunk", "[Terminal]") {
    ned::ui::EventLoop       eventLoop;
    std::vector<std::string> received;

    PtyProcess process(
        {"true"}, 24, 80, eventLoop, [&received](std::string_view chunk) { received.emplace_back(chunk); },
        [](std::optional<int>) {});

    process.DispatchOutput("hello");
    process.DispatchOutput("world");

    REQUIRE(received == std::vector<std::string>{"hello", "world"});
}

TEST_CASE("PtyProcess::DispatchExit invokes onExit with the given exit code", "[Terminal]") {
    ned::ui::EventLoop eventLoop;
    std::optional<int> received;
    bool               called = false;

    PtyProcess process(
        {"true"}, 24, 80, eventLoop, [](std::string_view) {},
        [&received, &called](std::optional<int> exitCode) {
            received = exitCode;
            called   = true;
        });

    process.DispatchExit(7);

    REQUIRE(called);
    REQUIRE(received == 7);
}

TEST_CASE("PtyProcess spawns with and propagates the requested pty size", "[Terminal]") {
    ned::ui::EventLoop eventLoop;

    PtyProcess process(
        {"sleep", "100"}, 24, 80, eventLoop, [](std::string_view) {}, [](std::optional<int>) {});

    winsize windowSize{};
    REQUIRE(::ioctl(process.MasterFdForTesting(), TIOCGWINSZ, &windowSize) == 0);
    REQUIRE(windowSize.ws_row == 24);
    REQUIRE(windowSize.ws_col == 80);

    process.Resize(30, 100);
    REQUIRE(::ioctl(process.MasterFdForTesting(), TIOCGWINSZ, &windowSize) == 0);
    REQUIRE(windowSize.ws_row == 30);
    REQUIRE(windowSize.ws_col == 100);
}

TEST_CASE("PtyProcess destruction does not hang on a long-lived child", "[Terminal]") {
    ned::ui::EventLoop eventLoop;

    // Nothing to assert -- the test is that this scope exits promptly: the
    // destructor's fd close delivers the session leader its SIGHUP, the
    // grace loop reaps it, and the read loop's blocked ::read returns so
    // the jthread join completes.
    PtyProcess process(
        {"sleep", "100"}, 24, 80, eventLoop, [](std::string_view) {}, [](std::optional<int>) {});
}

TEST_CASE("PtyProcess round-trips input to a real shell", "[Terminal]") {
    ned::ui::EventLoop eventLoop;
    std::string        output;

    PtyProcess process(
        {"sh", "-c", "read line; printf 'got:%s\\n' \"$line\""}, 24, 80, eventLoop,
        [&output](std::string_view chunk) { output += chunk; }, [](std::optional<int>) {});

    process.Write("ping\n");

    // The read loop marshals chunks through eventLoop.Post; with no Run()
    // loop alive, drain the queue by hand until the reply shows up (the pty
    // also echoes the input back -- only the shell's own reply is asserted).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (output.find("got:ping") == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        eventLoop.DrainPosted_();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(output.find("got:ping") != std::string::npos);
}

TEST_CASE("Constructing a PtyProcess for a nonexistent executable throws", "[Terminal]") {
    ned::ui::EventLoop eventLoop;

    REQUIRE_THROWS_AS(PtyProcess({"ned-definitely-not-a-real-binary-xyz"}, 24, 80, eventLoop, [](std::string_view) {}, [](std::optional<int>) {}), std::runtime_error);
}

TEST_CASE("Destroying a PtyProcess defuses its already-queued callbacks", "[Terminal]") {
    ned::ui::EventLoop eventLoop;
    int                outputs = 0;
    int                exits   = 0;

    {
        PtyProcess process(
            {"sh", "-c", "echo hi"}, 24, 80, eventLoop, [&outputs](std::string_view) { ++outputs; },
            [&exits](std::optional<int>) { ++exits; });
        // Give the child time to exit and the read loop time to queue its
        // Post-marshaled output/exit callbacks.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // The panel's [x] close button destroys a PtyProcess exactly like the
    // scope above -- mid-run, with callbacks still queued. Draining them now
    // must be inert: before the alive_ guard this was a real use-after-free
    // (a user-reported SIGSEGV/shutdown hang, and ASan-visible here).
    eventLoop.DrainPosted_();
    REQUIRE(outputs == 0);
    REQUIRE(exits == 0);
}
