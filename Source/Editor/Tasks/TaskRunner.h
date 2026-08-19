//
// Task runner follow-up. Process-wide manager owning every currently-running
// task (TaskProcess) -- analogous to Lsp/LspManager.h but considerably
// simpler: no JSON-RPC, no per-buffer sync state, just "spawn this
// configured command, stream its output into a buffer."
//
// Constructed once, alongside bufferList/lspManager, and passed by reference
// the same way -- task-runner state is shared editor-wide state, not
// something that belongs to one BufferView/window pane.
//

#ifndef NED_EDITOR_TASKS_TASKRUNNER_H
#define NED_EDITOR_TASKS_TASKRUNNER_H

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "TaskProcess.h"
#include "UI/EventLoop.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::tasks {

// The read-only, tossable buffer every run of `name` streams its output
// into -- "*task: <name>*", the same "*name*" bracket-naming convention
// Lsp/LspManager.h's kLspLogBufferName already established.
[[nodiscard]] std::string TaskOutputBufferName(std::string_view name);

class TaskRunner {
  public:
    // bufferList and eventLoop must both outlive this TaskRunner -- same
    // requirement LspManager's own constructor documents.
    TaskRunner(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop);
    ~TaskRunner() = default;

    TaskRunner(const TaskRunner&)            = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    // Looks up name via TaskConfig::TaskCommand; if unset, appends an error
    // line to the task's output buffer instead of spawning (no crash --
    // matches LspManager's own "nothing configured" non-error handling).
    // Otherwise finds-or-creates "*task: <name>*" (SetReadOnly(true) before
    // the first append; a "--- re-run ---" separator is appended first if
    // the buffer already has content from a prior run), spawns a
    // TaskProcess wired to stream into it, and returns a pointer to that
    // buffer so the caller (BufferView) can switch to it. Starting name
    // while it's already running is a no-op that returns the existing
    // buffer unchanged -- one concurrent run per task name.
    text::Buffer* RunTask(const std::string& name);

    // Cancels the running task named name, if any -- a no-op otherwise. The
    // same "\n[cancelled]\n" trailer/running_ cleanup that a natural exit
    // gets is handled uniformly by RunTask's own TaskProcess onExit
    // callback, not duplicated here.
    void CancelTask(const std::string& name);

    [[nodiscard]] bool IsRunning(const std::string& name) const;

  private:
    text::BufferList&   bufferList_;
    ned::ui::EventLoop& eventLoop_;

    std::unordered_map<std::string, std::unique_ptr<TaskProcess>> running_;
};

} // namespace ned::editor::tasks

#endif // NED_EDITOR_TASKS_TASKRUNNER_H
