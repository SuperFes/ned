#include "TestResultsBuffer.h"

#include <filesystem>
#include <vector>

#include "Editor/ProjectRoot.h"
#include "TestSourceResolver.h"
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
        else if (outcome.failuresOnly) {
            summary += FailuresOnlyHint(outcome.format);
        }
        return summary;
    }

    // test-runner-gaps follow-up: the path written here is the one
    // BufferView::VisitResultUnderPoint's regex will hand to
    // OpenOrCreateFile, so resolve it once at rebuild time -- where the
    // whole TestResult (including go's packagePath hint) is still in hand --
    // rather than leaving a bare "foo_test.go" to silently open an empty
    // scratch buffer on Enter. An unresolvable path is left exactly as the
    // framework reported it: still informative to read, and no worse than
    // before.
    std::string FormatResultLine(const TestResult& result, TestSourceResolver& resolver) {
        std::string line;
        const std::string file =
            result.file.empty()
                ? std::string()
                : resolver.Resolve(result.file, result.packagePath).value_or(std::filesystem::path(result.file)).string();
        if (!file.empty() && result.line != 0) {
            line += file + ":" + std::to_string(result.line) + ": ";
        }
        else if (!file.empty()) {
            line += file + ": ";
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
    // One resolver per rebuild: its project-tree index is built lazily and
    // at most once, so N failures in one module cost one walk, not N.
    TestSourceResolver                    resolver(ProjectRoot());
    std::size_t                           offset     = 0;
    const auto                            appendLine = [&](const std::string& text) {
        buffer->InsertAtPoint(text + "\n");
        offset += text.size() + 1;
    };
    const auto appendResultLine = [&](const TestResult& result) {
        const std::string line      = FormatResultLine(result, resolver);
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
