//
// A spawned child whose raw output is streamed to the main thread.
//
// A background jthread blocks in ChildProcess::ReadSome and posts each chunk
// onto a PostQueue, then reaps the child and posts its exit code. Both
// callbacks run on the main thread, and every public method is main-thread
// only. How the child was spawned (pipes, a pty) is the caller's business:
// it hands over an already-running ChildProcess.
//
// Teardown hangs up the child, which wakes the blocked read (and a Feed's
// blocked write), and joins both threads before the ChildProcess is
// destroyed; a stop_token alone cannot interrupt a read(). Callbacks still
// queued when this is destroyed are dropped.
//

#ifndef NED_EDITOR_PROCESS_STREAMEDCHILD_H
#define NED_EDITOR_PROCESS_STREAMEDCHILD_H

#include <chrono>
#include <functional>
#include <optional>
#include <string>
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

    // Writes input to the child's stdin from a thread of its own, then
    // closes it, so output is read while input still goes in: a remote
    // child's stdin moves through the main thread, and any child can stop
    // reading until its output is. A write waiting past timeout kills the
    // child, as does any other write failure, so a consumer never mistakes
    // output from part of its input for the whole. Once per child.
    void Feed(std::string input, std::chrono::milliseconds timeout);

    // What the reader's posted callbacks call. Public so tests can drive the
    // dispatch without draining a PostQueue.
    void DispatchOutput(std::string_view chunk);
    void DispatchExit(std::optional<int> exitCode);

  private:
    void StartReadLoop(PostQueue& postQueue);

    LifetimeGuard lifetime_;
    std::jthread  readThread_;
    std::jthread  writeThread_;
    ChildProcess  child_;
    OutputHandler onOutput_;
    ExitHandler   onExit_;
};

} // namespace ned::editor::process

#endif // NED_EDITOR_PROCESS_STREAMEDCHILD_H
