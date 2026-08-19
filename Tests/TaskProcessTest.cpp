#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <signal.h>

#include "Editor/Tasks/TaskProcess.h"
#include "UI/EventLoop.h"

using ned::editor::tasks::TaskProcess;

// Same rationale as LspClientTest.cpp's own ClientFixture comment: a real
// ned::ui::EventLoop is constructed (TaskProcess's constructor needs a real
// EventLoop&), but its Run() loop is never started -- every test here calls
// TaskProcess::DispatchOutput/DispatchExit directly instead, exercising the
// exact same onOutput_/onExit_ dispatch the background read thread's Post
// callbacks would otherwise reach, without needing a live loop draining
// posted work (see TaskProcess.h's own doc comment on why those methods are
// public). The real background thread each TaskProcess below spawns still
// runs concurrently against a real (trivial, short-lived) child process --
// harmless here since nothing drains this EventLoop's Post queue, so its
// own Post-marshaled calls into DispatchOutput/DispatchExit simply never
// fire; only this test's own direct calls do.

TEST_CASE("TaskProcess::DispatchOutput invokes onOutput with the given chunk", "[Tasks]") {
    ned::ui::EventLoop       eventLoop;
    std::vector<std::string> received;

    TaskProcess process(
        {"true"}, eventLoop, [&received](std::string_view chunk) { received.emplace_back(chunk); },
        [](std::optional<int>) {});

    process.DispatchOutput("hello");
    process.DispatchOutput("world");

    REQUIRE(received == std::vector<std::string>{"hello", "world"});
}

TEST_CASE("TaskProcess::DispatchExit invokes onExit with the given exit code", "[Tasks]") {
    ned::ui::EventLoop eventLoop;
    std::optional<int> received;
    bool               called = false;

    TaskProcess process(
        {"true"}, eventLoop, [](std::string_view) {},
        [&received, &called](std::optional<int> exitCode) {
            received = exitCode;
            called   = true;
        });

    process.DispatchExit(42);

    REQUIRE(called);
    REQUIRE(received.has_value());
    REQUIRE(*received == 42);
}

TEST_CASE("TaskProcess::DispatchExit invokes onExit with nullopt for a signaled/cancelled process", "[Tasks]") {
    ned::ui::EventLoop eventLoop;
    std::optional<int> received = 0; // seeded with a value to prove it gets overwritten to nullopt
    bool               called   = false;

    TaskProcess process(
        {"true"}, eventLoop, [](std::string_view) {},
        [&received, &called](std::optional<int> exitCode) {
            received = exitCode;
            called   = true;
        });

    process.DispatchExit(std::nullopt);

    REQUIRE(called);
    REQUIRE_FALSE(received.has_value());
}

TEST_CASE("TaskProcess::Cancel terminates a real long-running process promptly", "[Tasks]") {
    ned::ui::EventLoop eventLoop;

    TaskProcess process(
        {"sleep", "100"}, eventLoop, [](std::string_view) {}, [](std::optional<int>) {});

    // Just proving this returns promptly (doesn't hang the test) and that
    // destruction afterward doesn't hang either -- Cancel()'s own
    // ChildProcess::Kill() plus this TaskProcess's destructor (which joins
    // the background read thread) are what's under test here.
    process.Cancel();
}

TEST_CASE("Constructing a TaskProcess for a nonexistent executable throws", "[Tasks]") {
    ned::ui::EventLoop eventLoop;

    REQUIRE_THROWS_AS(
        TaskProcess({"ned-definitely-not-a-real-binary-xyz"}, eventLoop, [](std::string_view) {}, [](std::optional<int>) {}), std::runtime_error);
}
