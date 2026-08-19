#include "TaskProcess.h"

#include <utility>

namespace ned::editor::tasks {

TaskProcess::TaskProcess(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop, std::function<void(std::string_view)> onOutput,
                         std::function<void(std::optional<int>)> onExit) : child_(std::move(argv), process::StderrMode::MergeWithStdout), eventLoop_(eventLoop), onOutput_(std::move(onOutput)),
                                                                           onExit_(std::move(onExit)) {
    StartReadLoop();
}

void TaskProcess::StartReadLoop() {
    // child_ is already fully constructed by the time this runs (called
    // from the constructor *body*, after the member-initializer-list has
    // run) -- see TaskProcess.h's header comment for why readThread_ has to
    // start out empty (default-constructed) rather than being given real
    // work directly in the initializer list.
    readThread_ = std::jthread([this](std::stop_token) {
        while (true) {
            std::string chunk;
            try {
                chunk = child_.ReadSome(); // blocks
            }
            catch (const std::exception&) {
                // A genuine read error (as opposed to a clean EOF, which
                // ReadSome reports by returning an empty string) -- most
                // plausibly the fd being torn down out from under this
                // thread during shutdown. Treat it the same as EOF rather
                // than letting an uncaught exception escape a jthread body
                // (which would call std::terminate), mirroring LspClient's
                // own read-loop error handling.
                break;
            }
            if (chunk.empty()) {
                break; // EOF -- the process exited (or this TaskProcess is being destroyed)
            }
            eventLoop_.Post([this, chunk = std::move(chunk)]() mutable { DispatchOutput(chunk); });
        }
        const std::optional<int> exitCode = child_.WaitForExit();
        eventLoop_.Post([this, exitCode] { DispatchExit(exitCode); });
    });
}

void TaskProcess::Cancel() noexcept {
    child_.Kill();
}

void TaskProcess::DispatchOutput(std::string_view chunk) {
    if (onOutput_) {
        onOutput_(chunk);
    }
}

void TaskProcess::DispatchExit(std::optional<int> exitCode) {
    if (onExit_) {
        onExit_(exitCode);
    }
}

} // namespace ned::editor::tasks
