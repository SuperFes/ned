#include "TaskRunner.h"

#include <utility>

#include "Editor/DiagnosticsLog.h"
#include "TaskConfig.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::tasks {

std::string TaskOutputBufferName(std::string_view name) {
    return "*task: " + std::string(name) + "*";
}

TaskRunner::TaskRunner(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop) : bufferList_(bufferList), eventLoop_(eventLoop) {
}

text::Buffer* TaskRunner::RunTask(const std::string& name) {
    const std::string bufferName = TaskOutputBufferName(name);
    text::Buffer*     buffer     = bufferList_.Find(bufferName);
    if (!buffer) {
        buffer = &bufferList_.CreateBuffer(bufferName);
        buffer->SetReadOnly(true); // must be set before the first append -- AppendWhileReadOnly's own precondition
    }

    if (running_.contains(name)) {
        return buffer; // already running -- no-op, existing run keeps streaming into the same buffer
    }

    if (!buffer->Text().empty()) {
        buffer->AppendWhileReadOnly("\n--- re-run ---\n");
    }

    const std::optional<std::vector<std::string>> argv = TaskCommand(name);
    if (!argv) {
        buffer->AppendWhileReadOnly("No command configured for task \"" + name + "\" (see ned/set-task-command).\n");
        return buffer;
    }

    try {
        running_[name] = std::make_unique<TaskProcess>(
            *argv, eventLoop_,
            [buffer](std::string_view chunk) { buffer->AppendWhileReadOnly(chunk); },
            [this, name, buffer](std::optional<int> exitCode) {
                if (exitCode) {
                    buffer->AppendWhileReadOnly("\n[exited " + std::to_string(*exitCode) + "]\n");
                    // diagnostics-log-round-2 follow-up: a durable record of
                    // task failures alongside the buffer's own transient
                    // output -- 0 is success, so only a nonzero exit is
                    // logged.
                    if (*exitCode != 0) {
                        LogMessage(LogCategory::Task, LogSeverity::Error,
                                   "task \"" + name + "\" exited " + std::to_string(*exitCode));
                    }
                }
                else {
                    buffer->AppendWhileReadOnly("\n[cancelled]\n");
                }
                running_.erase(name);
            });
    }
    catch (const std::exception& e) {
        const std::string message = "Failed to start task \"" + name + "\": " + e.what();
        LogMessage(LogCategory::Task, LogSeverity::Error, message);
        buffer->AppendWhileReadOnly(message + "\n");
    }

    return buffer;
}

void TaskRunner::CancelTask(const std::string& name) {
    const auto it = running_.find(name);
    if (it != running_.end()) {
        it->second->Cancel();
    }
}

bool TaskRunner::IsRunning(const std::string& name) const {
    return running_.contains(name);
}

} // namespace ned::editor::tasks
