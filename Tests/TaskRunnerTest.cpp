#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Tasks/TaskConfig.h"
#include "Editor/Tasks/TaskRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::tasks::SetTaskCommand;
using ned::editor::tasks::TaskOutputBufferName;
using ned::editor::tasks::TaskRunner;
using ned::text::Buffer;
using ned::text::BufferList;

// Same rationale as TaskProcessTest.cpp's own header comment: a real
// ned::ui::EventLoop is constructed (TaskRunner needs a real EventLoop& to
// hand each TaskProcess it spawns), but its Run() loop is never started, so
// a spawned real process's streamed output/exit trailer never actually
// lands in the task's buffer within these tests -- only RunTask's own
// synchronous behavior (buffer creation, no-command-configured handling,
// re-run separators, already-running no-op) is under test here, matching
// this codebase's established "never run a real EventLoop::Run() loop in a
// unit test" convention (see LspClientTest.cpp/LspManagerTest.cpp).

TEST_CASE("TaskOutputBufferName wraps the task name in the *task: ...* convention", "[Tasks]") {
    REQUIRE(TaskOutputBufferName("build") == "*task: build*");
}

TEST_CASE("RunTask on an unconfigured task name creates a read-only buffer reporting the error", "[Tasks]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TaskRunner         runner(bufferList, eventLoop);

    Buffer* buffer = runner.RunTask("task-runner-test-unconfigured");
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->ReadOnly());
    REQUIRE(buffer->Text().find("No command configured") != std::string::npos);
    REQUIRE_FALSE(runner.IsRunning("task-runner-test-unconfigured"));
}

TEST_CASE("Re-running an unconfigured task appends a re-run separator instead of clearing the buffer", "[Tasks]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TaskRunner         runner(bufferList, eventLoop);

    Buffer* first  = runner.RunTask("task-runner-test-rerun");
    Buffer* second = runner.RunTask("task-runner-test-rerun");

    REQUIRE(first == second); // same buffer reused, not replaced
    REQUIRE(second->Text().find("--- re-run ---") != std::string::npos);
}

TEST_CASE("RunTask on a task already running is a no-op that returns the existing buffer", "[Tasks]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TaskRunner         runner(bufferList, eventLoop);

    SetTaskCommand("task-runner-test-already-running", {"sleep", "100"});

    Buffer* first = runner.RunTask("task-runner-test-already-running");
    REQUIRE(runner.IsRunning("task-runner-test-already-running"));

    Buffer* second = runner.RunTask("task-runner-test-already-running");
    REQUIRE(first == second);
    // No re-run separator -- the still-running task's own buffer is left
    // untouched by the second call, matching RunTask's documented no-op.
    REQUIRE(second->Text().find("--- re-run ---") == std::string::npos);

    runner.CancelTask("task-runner-test-already-running");
    SetTaskCommand("task-runner-test-already-running", {}); // cleanup -- process-wide state
}

TEST_CASE("CancelTask on a task that isn't running is a safe no-op", "[Tasks]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    TaskRunner         runner(bufferList, eventLoop);

    runner.CancelTask("task-runner-test-never-started"); // must not throw/crash
    REQUIRE_FALSE(runner.IsRunning("task-runner-test-never-started"));
}
