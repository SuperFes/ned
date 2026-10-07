//
// Task runner follow-up. Process-wide manager owning every currently-running
// task (TaskProcess) -- analogous to Lsp/Manager.h but considerably
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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Editor/Lifetime.h"
#include "Editor/Remote/Placement.h"
#include "TaskProcess.h"
#include "Text/RemoteFile.h"
#include "UI/EventLoop.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::tasks {

// The read-only, tossable buffer every run of `name` streams its output
// into -- "*task: <name>*", the same "*name*" bracket-naming convention
// Lsp/Manager.h's kLspLogBufferName already established.
[[nodiscard]] std::string TaskOutputBufferName(std::string_view name);

class TaskRunner {
  public:
    // bufferList and eventLoop must both outlive this TaskRunner -- same
    // requirement Manager's own constructor documents.
    TaskRunner(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop);
    ~TaskRunner() = default;

    TaskRunner(const TaskRunner&)            = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    // Looks up name via TaskConfig::TaskCommand; if unset, appends an error
    // line to the task's output buffer instead of spawning (no crash --
    // matches Manager's own "nothing configured" non-error handling).
    // Otherwise finds-or-creates "*task: <name>*" (SetReadOnly(true) before
    // the first append; a "--- re-run ---" separator is appended first if
    // the buffer already has content from a prior run), spawns a
    // TaskProcess wired to stream into it, and returns a pointer to that
    // buffer so the caller (BufferView) can switch to it. Starting name
    // while it's already running is a no-op that returns the existing
    // buffer unchanged -- one concurrent run per task name.
    //
    // The task runs beside from (Remote/Placement.h): on its host, in the
    // project root there, for a remote buffer. Without one, or from a
    // buffer with no file, such as its own output, it runs where it last did.
    text::Buffer* RunTask(const std::string& name, const text::Buffer* from = nullptr);

    // Null, the default, runs every task on this machine. hosts must
    // outlive this TaskRunner.
    void SetHosts(remote::Hosts* hosts);

    // Cancels the running task named name, if any -- a no-op otherwise. The
    // same "\n[cancelled]\n" trailer/running_ cleanup that a natural exit
    // gets is handled uniformly by RunTask's own TaskProcess onExit
    // callback, not duplicated here.
    void CancelTask(const std::string& name);

    [[nodiscard]] bool IsRunning(const std::string& name) const;

  private:
    // Found by name on every call, so output from a run whose buffer was
    // closed lands in a recreated one instead of a freed one.
    text::Buffer& OutputBuffer(const std::string& name);
    [[nodiscard]] std::optional<text::RemoteFile> PlacementFrom(const std::string& name, const text::Buffer* from) const;
    void                                          Started(const std::string& name, remote::Spawned spawned);

    // A run is starting, possibly on its host, until it has a process;
    // cancelling it then drops starting, so nothing is spawned for it.
    struct Run {
        std::unique_ptr<LifetimeGuard> starting;
        std::unique_ptr<TaskProcess>   process;
    };

    text::BufferList&   bufferList_;
    ned::ui::EventLoop& eventLoop_;
    remote::Hosts*      hosts_ = nullptr;

    std::unordered_map<std::string, Run>                             running_;
    std::unordered_map<std::string, std::optional<text::RemoteFile>> placedAt_;
};

} // namespace ned::editor::tasks

#endif // NED_EDITOR_TASKS_TASKRUNNER_H
