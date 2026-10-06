#include "MessagesBuffer.h"

#include <string>
#include <utility>
#include <vector>

#include "Editor/DiagnosticsLog.h"

namespace ned::editor {

namespace {

    text::Buffer::Diagnostic::Severity DiagnosticSeverityFor(LogSeverity severity) {
        switch (severity) {
            case LogSeverity::Info:
                return text::Buffer::Diagnostic::Severity::Information;
            case LogSeverity::Warning:
                return text::Buffer::Diagnostic::Severity::Warning;
            case LogSeverity::Error:
                return text::Buffer::Diagnostic::Severity::Error;
        }
        return text::Buffer::Diagnostic::Severity::Information; // unreachable
    }

} // namespace

std::string_view MessagesBufferName() {
    return "*Messages*";
}

void RebuildMessagesBuffer(text::BufferList& bufferList) {
    const std::string bufferName(MessagesBufferName());
    text::Buffer*     buffer = bufferList.Find(bufferName);
    if (!buffer) {
        buffer = &bufferList.CreateBuffer(bufferName);
        buffer->SetReadOnly(true); // must be set before the first append -- AppendWhileReadOnly's own precondition
    }

    std::vector<LogEntry> visible;
    for (LogEntry& entry : LogEntries()) {
        if (LogCategoryVisible(entry.category)) {
            visible.push_back(std::move(entry));
        }
    }

    buffer->SetReadOnly(false);
    buffer->BeginUndoGroup();
    if (buffer->Size() > 0) {
        buffer->DeleteRange(0, buffer->Size());
    }

    std::vector<text::Buffer::Diagnostic> diagnostics;
    diagnostics.reserve(visible.size());
    std::size_t offset = 0;
    for (const LogEntry& entry : visible) {
        const std::string line      = FormatLogLine(entry) + "\n";
        const std::size_t startByte = offset;
        buffer->InsertAtPoint(line);
        offset += line.size();
        // -1 excludes the trailing newline from the diagnostic's own range,
        // matching every other line-ranged Diagnostic in this codebase.
        diagnostics.push_back(text::Buffer::Diagnostic{
            .startByte = startByte,
            .endByte   = offset > 0 ? offset - 1 : offset,
            .severity  = DiagnosticSeverityFor(entry.severity),
            .origin    = text::Buffer::Diagnostic::Origin::Code,
            .message   = entry.message + LogCountSuffix(entry.count),
        });
    }
    buffer->SetPoint(buffer->Size());
    buffer->EndUndoGroup();
    buffer->SetReadOnly(true);
    buffer->SetDiagnostics(std::move(diagnostics));
}

} // namespace ned::editor
