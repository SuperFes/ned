#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <unistd.h>

#include "Editor/Tasks/TaskProcess.h"
#include "UI/EventLoop.h"

using ned::editor::tasks::TaskProcess;

// Same rationale as ClientTest.cpp's own ClientFixture comment: a real
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

// A process can be destroyed with its output and exit still queued on the
// loop; neither may run against it afterwards.
TEST_CASE("A TaskProcess destroyed with its output still queued delivers nothing", "[Tasks][Lifetime]") {
    ned::ui::EventLoop eventLoop;
    int                outputs = 0;
    int                exits   = 0;

    std::optional<TaskProcess> process(
        std::in_place, std::vector<std::string>{"echo", "queued"}, eventLoop, [&outputs](std::string_view) { ++outputs; },
        [&exits](std::optional<int>) { ++exits; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // output and exit both posted
    process.reset();
    (void)eventLoop.DrainPosted_();

    CHECK(outputs == 0);
    CHECK(exits == 0);
}

// A task can leave behind a process outside its group (setsid) that still
// holds the output pipe; killing the task's group never closes it.
TEST_CASE("Destroying a TaskProcess returns promptly while a detached grandchild holds its output open", "[Tasks][Lifetime]") {
    ned::ui::EventLoop          eventLoop;
    const std::filesystem::path pidFile = std::filesystem::temp_directory_path() / ("ned-task-grandchild-" + std::to_string(::getpid()));
    std::filesystem::remove(pidFile);

    std::optional<TaskProcess> process(
        std::in_place, std::vector<std::string>{"sh", "-c", "setsid sleep 30 & echo $! > '" + pidFile.string() + "'; exec sleep 100"}, eventLoop,
        [](std::string_view) {}, [](std::optional<int>) {});

    pid_t grandchild = -1;
    for (int attempt = 0; attempt < 200 && grandchild <= 0; ++attempt) {
        std::ifstream in(pidFile);
        if (!(in >> grandchild)) {
            grandchild = -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    REQUIRE(grandchild > 0);

    std::atomic<bool> destroyed{false};
    std::thread       destroyer([&] {
        process.reset();
        destroyed = true;
    });
    const auto        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!destroyed && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool prompt = destroyed.load();
    ::kill(grandchild, SIGKILL); // also what unblocks a hung destroyer
    destroyer.join();
    std::filesystem::remove(pidFile);

    REQUIRE(prompt);
}
