#include "TaskProcess.h"

#include <utility>

namespace ned::editor::tasks {

TaskProcess::TaskProcess(std::vector<std::string> argv, ned::ui::EventLoop& eventLoop, std::function<void(std::string_view)> onOutput,
                         std::function<void(std::optional<int>)> onExit, process::StderrMode stderrMode) : stream_(process::ChildProcess(argv, stderrMode), eventLoop, std::move(onOutput), std::move(onExit)) {
}

TaskProcess::TaskProcess(process::ChildProcess child, ned::ui::EventLoop& eventLoop, std::function<void(std::string_view)> onOutput,
                         std::function<void(std::optional<int>)> onExit) : stream_(std::move(child), eventLoop, std::move(onOutput), std::move(onExit)) {
}

void TaskProcess::Cancel() noexcept {
    stream_.Child().Kill();
}

void TaskProcess::Feed(std::string input, std::chrono::milliseconds timeout) {
    stream_.Feed(std::move(input), timeout);
}

void TaskProcess::DispatchOutput(std::string_view chunk) {
    stream_.DispatchOutput(chunk);
}

void TaskProcess::DispatchExit(std::optional<int> exitCode) {
    stream_.DispatchExit(exitCode);
}

} // namespace ned::editor::tasks
