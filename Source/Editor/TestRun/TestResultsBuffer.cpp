#include "TestResultsBuffer.h"

#include <vector>

#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::testrun {

namespace {

    std::string FormatSummary(const TestRunOutcome& outcome) {
        std::string summary = "Tests: " + std::to_string(outcome.passed) + " passed, " + std::to_string(outcome.failed) +
                              " failed, " + std::to_string(outcome.skipped) + " skipped";
        if (!outcome.format.empty()) {
            summary += " (" + outcome.format + ")";
        }
        if (!outcome.parsedOk) {
            summary += " -- output did not match this format";
        }
        return summary;
    }

    std::string FormatResultLine(const TestResult& result) {
        std::string line;
        if (!result.file.empty() && result.line != 0) {
            line += result.file + ":" + std::to_string(result.line) + ": ";
        }
        else if (!result.file.empty()) {
            line += result.file + ": ";
        }
        line += result.status == TestResult::Status::Failed ? "[FAILED] " : "[SKIPPED] ";
        line += result.name;
        if (!result.message.empty()) {
            line += " -- " + result.message;
        }
        return line;
    }

} // namespace

std::string TestResultsBufferName() {
    return "*test results*";
}

text::Buffer& RebuildTestResultsBuffer(text::BufferList& bufferList, const TestRunOutcome& outcome) {
    text::Buffer* buffer = bufferList.Find(TestResultsBufferName());
    if (!buffer) {
        buffer = &bufferList.CreateBuffer(TestResultsBufferName());
        buffer->SetReadOnly(true); // before the first append -- AppendWhileReadOnly's precondition
    }

    buffer->SetReadOnly(false);
    buffer->BeginUndoGroup();
    if (buffer->Size() > 0) {
        buffer->DeleteRange(0, buffer->Size());
    }

    std::vector<text::Buffer::Diagnostic> diagnostics;
    std::size_t                           offset     = 0;
    const auto                            appendLine = [&](const std::string& text) {
        buffer->InsertAtPoint(text + "\n");
        offset += text.size() + 1;
    };
    const auto appendResultLine = [&](const TestResult& result) {
        const std::string line      = FormatResultLine(result);
        const std::size_t startByte = offset;
        appendLine(line);
        // -1 excludes the trailing newline from the diagnostic's own range,
        // matching every other line-ranged Diagnostic in this codebase.
        diagnostics.push_back(text::Buffer::Diagnostic{
            .startByte = startByte,
            .endByte   = offset > 0 ? offset - 1 : offset,
            .severity  = result.status == TestResult::Status::Failed ? text::Buffer::Diagnostic::Severity::Error
                                                                     : text::Buffer::Diagnostic::Severity::Warning,
            .origin    = text::Buffer::Diagnostic::Origin::Code,
            .message   = result.message.empty() ? result.name : result.message,
        });
    };

    appendLine(FormatSummary(outcome));
    appendLine("");

    // Failed first (the worklist), then skipped; passed omitted entirely.
    for (const TestResult& result : outcome.results) {
        if (result.status == TestResult::Status::Failed) {
            appendResultLine(result);
        }
    }
    for (const TestResult& result : outcome.results) {
        if (result.status == TestResult::Status::Skipped) {
            appendResultLine(result);
        }
    }
    if (diagnostics.empty() && outcome.parsedOk) {
        appendLine("All tests passed.");
    }

    // Point on the first result line (right after summary + blank), so the
    // first failure is immediately visible/visitable.
    buffer->SetPoint(diagnostics.empty() ? 0 : diagnostics.front().startByte);
    buffer->EndUndoGroup();
    buffer->SetReadOnly(true);
    buffer->SetDiagnostics(std::move(diagnostics));
    return *buffer;
}

} // namespace ned::editor::testrun
