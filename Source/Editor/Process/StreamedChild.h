//
// A spawned child whose raw output is streamed to the main thread.
//
// A background jthread blocks in ChildProcess::ReadSome and posts each chunk
// onto a PostQueue, then reaps the child and posts its exit code. Both
// callbacks run on the main thread, and every public method is main-thread
// only. How the child was spawned (pipes, a pty) is the caller's business:
// it hands over an already-running ChildProcess.
//
// Teardown hangs up the child, which wakes the blocked read, and joins the
// reader before the ChildProcess is destroyed; a stop_token alone cannot
// interrupt a read(). Callbacks still queued when this is destroyed are
// dropped.
//

#ifndef NED_EDITOR_PROCESS_STREAMEDCHILD_H
#define NED_EDITOR_PROCESS_STREAMEDCHILD_H

#include <functional>
#include <optional>
#include <string_view>
#include <thread>

#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Process/ChildProcess.h"

namespace ned::editor::process {

class StreamedChild {
  public:
    using OutputHandler = std::function<void(std::string_view chunk)>;
    // exitCode is std::nullopt when the child was terminated by a signal.
    using ExitHandler = std::function<void(std::optional<int> exitCode)>;

    // postQueue must outlive this object.
    StreamedChild(ChildProcess child, PostQueue& postQueue, OutputHandler onOutput, ExitHandler onExit);
    ~StreamedChild();

    StreamedChild(const StreamedChild&)            = delete;
    StreamedChild& operator=(const StreamedChild&) = delete;
    // Not movable: the reader thread captures `this`.
    StreamedChild(StreamedChild&&)            = delete;
    StreamedChild& operator=(StreamedChild&&) = delete;

    [[nodiscard]] ChildProcess&       Child() noexcept;
    [[nodiscard]] const ChildProcess& Child() const noexcept;

    // What the reader's posted callbacks call. Public so tests can drive the
    // dispatch without draining a PostQueue.
    void DispatchOutput(std::string_view chunk);
    void DispatchExit(std::optional<int> exitCode);

  private:
    void StartReadLoop(PostQueue& postQueue);

    LifetimeGuard lifetime_;
    std::jthread  readThread_;
    ChildProcess  child_;
    OutputHandler onOutput_;
    ExitHandler   onExit_;
};

} // namespace ned::editor::process

#endif // NED_EDITOR_PROCESS_STREAMEDCHILD_H
