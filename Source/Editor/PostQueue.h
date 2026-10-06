//
// Thread-safe "run this on the owning thread" queue with a pollable wakeup fd.
//
// Background threads (protocol read loops, child-process readers, timers)
// never touch main-thread state directly; they Post() a callback here and the
// owning thread runs it from DrainPosted_(). The owner's loop polls WakeFd()
// alongside whatever else it waits on. ui::EventLoop is one such owner; a
// headless loop with no terminal is another, which is why this lives apart
// from Notcurses.
//
// The wake pipe is only ever a signal. posted_ carries the work, so a full
// pipe loses nothing.
//

#ifndef NED_EDITOR_POSTQUEUE_H
#define NED_EDITOR_POSTQUEUE_H

#include <functional>
#include <mutex>
#include <vector>

namespace ned::editor {

class PostQueue {
  public:
    // Throws std::runtime_error if the wake pipe can't be created.
    PostQueue();
    ~PostQueue();

    PostQueue(const PostQueue&)            = delete;
    PostQueue& operator=(const PostQueue&) = delete;

    // Callable from any thread.
    void Post(std::function<void()> fn);

    // Runs every currently-queued callback on the calling thread and returns
    // whether any ran. Work posted by a running callback waits for the next
    // drain. A callback that throws is logged, not propagated. Production
    // callers are the owning loops; tests use it to pump posted work with no
    // loop running.
    bool DrainPosted_();

    // Readable whenever Post() or Wake() has run since the last
    // DiscardWakeups().
    [[nodiscard]] int WakeFd() const noexcept {
        return wakeReadFd_;
    }

    // Wakes a poll() on WakeFd() without queueing work. Callable from any
    // thread.
    void Wake() noexcept;

    void DiscardWakeups() noexcept;

  private:
    int wakeReadFd_  = -1;
    int wakeWriteFd_ = -1;

    std::mutex                         postedMutex_;
    std::vector<std::function<void()>> posted_;
};

} // namespace ned::editor

#endif // NED_EDITOR_POSTQUEUE_H
