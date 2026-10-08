//
// Task runner follow-up. Owns one running task's subprocess and streams its
// raw combined stdout+stderr to a caller-supplied callback -- no JSON-RPC,
// no framing at all (see Process/ChildProcess.h for the raw spawn/pipe layer
// this sits on top of, and Lsp/Client.h for the framed sibling this
// deliberately mirrors the shape of).
//
// The read loop, its main-thread marshaling and the teardown order live in
// Process/StreamedChild.h; this adds only the spawn and Cancel().
//

#ifndef NED_EDITOR_TASKS_TASKPROCESS_H
#define NED_EDITOR_TASKS_TASKPROCESS_H

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Process/StreamedChild.h"
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
    // One already started, wherever it runs (Remote/Placement.h).
    TaskProcess(process::ChildProcess child, ned::ui::EventLoop& eventLoop, std::function<void(std::string_view chunk)> onOutput,
                std::function<void(std::optional<int> exitCode)> onExit);

    TaskProcess(const TaskProcess&)            = delete;
    TaskProcess& operator=(const TaskProcess&) = delete;

    // User-triggered cancellation (cancel-task) -- SIGKILLs the child. The
    // background read loop's own ReadSome() then observes EOF as usual and
    // fires onExit(std::nullopt) exactly the same way a natural exit fires
    // onExit(exitCode), so callers don't need to distinguish how they
    // learned the process ended.
    void Cancel() noexcept;

    // StreamedChild::Feed.
    void Feed(std::string input, std::chrono::milliseconds timeout);

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
    process::StreamedChild stream_;
};

} // namespace ned::editor::tasks

#endif // NED_EDITOR_TASKS_TASKPROCESS_H
