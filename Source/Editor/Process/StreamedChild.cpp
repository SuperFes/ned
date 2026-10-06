#include "StreamedChild.h"

#include <exception>
#include <string>
#include <utility>


namespace ned::editor::process {

StreamedChild::StreamedChild(ChildProcess child, PostQueue& postQueue, OutputHandler onOutput, ExitHandler onExit) : child_(std::move(child)), onOutput_(std::move(onOutput)), onExit_(std::move(onExit)) {
    // Started in the body, once child_ exists, which is why readThread_ is
    // declared ahead of it empty.
    StartReadLoop(postQueue);
}

StreamedChild::~StreamedChild() {
    child_.CloseConnection();
    if (readThread_.joinable()) {
        readThread_.join();
    }
}

ChildProcess& StreamedChild::Child() noexcept {
    return child_;
}

const ChildProcess& StreamedChild::Child() const noexcept {
    return child_;
}

void StreamedChild::DispatchOutput(std::string_view chunk) {
    if (onOutput_) {
        onOutput_(chunk);
    }
}

void StreamedChild::DispatchExit(std::optional<int> exitCode) {
    if (onExit_) {
        onExit_(exitCode);
    }
}

void StreamedChild::StartReadLoop(PostQueue& postQueue) {
    readThread_ = std::jthread([this, &postQueue, alive = lifetime_.Token()](std::stop_token) {
        while (true) {
            std::string chunk;
            try {
                // Blocks. A pty's EIO at the child's exit reads as EOF.
                chunk = child_.ReadSome();
            }
            catch (const std::exception&) {
                // Most plausibly the fd torn down under this thread during
                // teardown; an exception escaping a jthread would terminate.
                break;
            }
            if (chunk.empty()) {
                break; // EOF: the child exited, or this object is being destroyed
            }
            postQueue.Post(alive.Bind([this, chunk = std::move(chunk)] { DispatchOutput(chunk); }));
        }
        const std::optional<int> exitCode = child_.WaitForExit();
        postQueue.Post(alive.Bind([this, exitCode] { DispatchExit(exitCode); }));
    });
}

} // namespace ned::editor::process
