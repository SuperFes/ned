//
// Task runner follow-up. Owns one running task's subprocess and streams its
// raw combined stdout+stderr to a caller-supplied callback -- no JSON-RPC,
// no framing at all (see Process/ChildProcess.h for the raw spawn/pipe layer
// this sits on top of, and Lsp/Client.h for the framed sibling this
// deliberately mirrors the shape of).
//
// Threading/lifetime notes are identical to Client's own -- read that
// file's header comment for the full reasoning; the short version:
// - A background std::jthread runs a blocking read loop (ChildProcess::
//   ReadSome), marshaling each chunk onto the main thread via
//   ned::ui::EventLoop::Post. Every public method here only ever runs on
//   the main thread.
// - The destructor hangs up child_, which wakes the read loop, and joins
//   it before child_ itself is destroyed -- see ChildProcess's threading
//   note. A stop_token alone cannot interrupt a blocking read().
//

#ifndef NED_EDITOR_TASKS_TASKPROCESS_H
#define NED_EDITOR_TASKS_TASKPROCESS_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Editor/Lifetime.h"
#include "Editor/Process/ChildProcess.h"
#include "UI/EventLoop.h"

namespace ned::editor::tasks {

class TaskProcess {
  public:
    // onOutput is called on the main thread with each raw chunk of combined
    // stdout+stderr as it arrives. onExit is called exactly once, also on
    // the main thread, once the process has exited (or been Cancel()ed) --
    // exitCode is the process's real exit code, or std::nullopt if it was
    // terminated by a signal (Cancel(), most commonly). eventLoop must
    // outlive this TaskProcess, same requirement Client's own
    // constructor documents. stderrMode Discard keeps a command's
    // diagnostics out of output that must stay clean (a printed token).
    TaskProcess(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop, std::function<void(std::string_view chunk)> onOutput,
                std::function<void(std::optional<int> exitCode)> onExit,
                process::StderrMode                              stderrMode = process::StderrMode::MergeWithStdout);

    ~TaskProcess();

    TaskProcess(const TaskProcess&)            = delete;
    TaskProcess& operator=(const TaskProcess&) = delete;
    // Not movable: the background thread's lambda captures `this` directly.
    TaskProcess(TaskProcess&&)            = delete;
    TaskProcess& operator=(TaskProcess&&) = delete;

    // User-triggered cancellation (cancel-task) -- SIGKILLs the child. The
    // background read loop's own ReadSome() then observes EOF as usual and
    // fires onExit(std::nullopt) exactly the same way a natural exit fires
    // onExit(exitCode), so callers don't need to distinguish how they
    // learned the process ended.
    void Cancel() noexcept;

    // Public primarily for tests -- mirrors Client::DispatchFrame's own
    // "public primarily for tests" precedent (see that method's doc comment
    // in Client.h): the real background read loop always reaches these
    // via eventLoop_.Post, but exercising that requires a real, running
    // EventLoop::Run() loop (no synchronous fallback exists). Calling these
    // directly exercises the exact same onOutput_/onExit_ dispatch without
    // needing one.
    void DispatchOutput(std::string_view chunk);
    void DispatchExit(std::optional<int> exitCode);

  private:
    void StartReadLoop();

    // Drops output and exit already posted when this process is destroyed.
    editor::LifetimeGuard lifetime_;
    std::jthread          readThread_;
    process::ChildProcess child_;

    ned::ui::EventLoop& eventLoop_;

    std::function<void(std::string_view chunk)>      onOutput_;
    std::function<void(std::optional<int> exitCode)> onExit_;
};

} // namespace ned::editor::tasks

#endif // NED_EDITOR_TASKS_TASKPROCESS_H
