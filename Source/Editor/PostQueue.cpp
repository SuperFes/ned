#include "PostQueue.h"

#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "Editor/DiagnosticsLog.h"

namespace ned::editor {

PostQueue::PostQueue() {
    int fds[2];
    if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        throw std::runtime_error("PostQueue: pipe2() failed");
    }
    wakeReadFd_  = fds[0];
    wakeWriteFd_ = fds[1];
}

PostQueue::~PostQueue() {
    close(wakeReadFd_);
    close(wakeWriteFd_);
}

void PostQueue::Post(std::function<void()> fn) {
    {
        const std::lock_guard<std::mutex> lock(postedMutex_);
        posted_.push_back(std::move(fn));
    }
    Wake();
}

bool PostQueue::DrainPosted_() {
    std::vector<std::function<void()>> work;
    {
        const std::lock_guard<std::mutex> lock(postedMutex_);
        work.swap(posted_);
    }
    for (auto& fn : work) {
        try {
            fn();
        }
        catch (const std::exception& e) {
            LogMessage(LogCategory::General, LogSeverity::Error,
                       std::string("ned: uncaught exception in posted background work: ") + e.what());
        }
        catch (...) {
            LogMessage(LogCategory::General, LogSeverity::Error,
                       "ned: uncaught non-std::exception in posted background work");
        }
    }
    return !work.empty();
}

void PostQueue::Wake() noexcept {
    const char                     byte    = 0;
    [[maybe_unused]] const ssize_t written = write(wakeWriteFd_, &byte, 1);
}

void PostQueue::DiscardWakeups() noexcept {
    char discard[64];
    while (read(wakeReadFd_, discard, sizeof(discard)) > 0) {
    }
}

} // namespace ned::editor
