#include "TaskRunner.h"

#include <memory>
#include <optional>
#include <utility>

#include "Editor/DiagnosticsLog.h"
#include "Editor/SanitizerOutputParser.h"
#include "Editor/ValgrindOutputParser.h"
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
        // sanitizer-output-parser follow-up: accumulated alongside the
        // streamed buffer append (not read back from buffer->Text(), which
        // may already carry prior runs' own "--- re-run ---"-separated
        // history) so a nonzero-exit run's own output can be scanned for an
        // ASan/UBSan/TSan/MSan/LSan report on exit.
        auto accumulated = std::make_shared<std::string>();
        running_[name]   = std::make_unique<TaskProcess>(
            *argv, eventLoop_,
            [buffer, accumulated](std::string_view chunk) {
                buffer->AppendWhileReadOnly(chunk);
                *accumulated += chunk;
            },
            [this, name, buffer, accumulated](std::optional<int> exitCode) {
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
                    for (SanitizerFinding& finding : ParseSanitizerOutput(*accumulated)) {
                        std::string message = finding.tool + ": " + finding.message;
                        if (!finding.symbol.empty()) {
                            message += " (in " + finding.symbol + ")";
                        }
                        LogMessage(LogCategory::Task, LogSeverity::Error, "task \"" + name + "\": " + message,
                                   finding.file.empty() ? std::nullopt : std::make_optional(std::move(finding.file)),
                                   finding.line == 0 ? std::nullopt : std::make_optional(finding.line));
                    }
                    // valgrind-xml-parser follow-up: a task pointed at
                    // `valgrind --xml=yes ...` gets the same durable,
                    // clickable finding record the sanitizer parser above
                    // already gives a `-fsanitize=...` build.
                    for (ValgrindFinding& finding : ParseValgrindXml(*accumulated)) {
                        std::string message = finding.tool.empty() ? "valgrind" : finding.tool;
                        if (!finding.kind.empty()) {
                            message += ": " + finding.kind;
                        }
                        if (!finding.message.empty()) {
                            message += " (" + finding.message + ")";
                        }
                        LogMessage(LogCategory::Task, LogSeverity::Error, "task \"" + name + "\": " + message,
                                   finding.file.empty() ? std::nullopt : std::make_optional(std::move(finding.file)),
                                   finding.line == 0 ? std::nullopt : std::make_optional(finding.line));
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
