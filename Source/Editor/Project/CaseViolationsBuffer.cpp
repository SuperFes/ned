#include "CaseViolationsBuffer.h"

#include "Editor/FormatRules.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor {

namespace {

    std::string FormatViolationLine(const ProjectCaseViolation& v) {
        std::string line = v.file.string() + ":" + std::to_string(v.line) + ": [" + v.violation.entityKind + "] \"" +
                           v.violation.name + "\" is not " + CaseConventionName(v.violation.expectedConvention);
        if (!v.violation.suggestedName.empty()) {
            line += " -- suggest \"" + v.violation.suggestedName + "\"";
        }
        return line;
    }

} // namespace

std::string CaseViolationsBufferName() {
    return "*case violations*";
}

text::Buffer& RebuildCaseViolationsBuffer(text::BufferList& bufferList, const std::vector<ProjectCaseViolation>& violations) {
    text::Buffer* buffer = bufferList.Find(CaseViolationsBufferName());
    if (!buffer) {
        buffer = &bufferList.CreateBuffer(CaseViolationsBufferName());
        buffer->SetReadOnly(true); // before the first append -- AppendWhileReadOnly's precondition
    }

    buffer->SetReadOnly(false);
    buffer->BeginUndoGroup();
    if (buffer->Size() > 0) {
        buffer->DeleteRange(0, buffer->Size());
    }

    std::vector<text::Buffer::Diagnostic> diagnostics;
    std::size_t                           offset     = 0;
    const auto                            appendLine = [&](const std::string& line) {
        buffer->InsertAtPoint(line + "\n");
        offset += line.size() + 1;
    };

    appendLine("Case-convention violations: " + std::to_string(violations.size()) + " found");
    appendLine("");

    for (const ProjectCaseViolation& v : violations) {
        const std::string line      = FormatViolationLine(v);
        const std::size_t startByte = offset;
        appendLine(line);
        // -1 excludes the trailing newline -- every other line-ranged
        // Diagnostic in this codebase does the same.
        diagnostics.push_back(text::Buffer::Diagnostic{
            .startByte = startByte,
            .endByte   = offset > 0 ? offset - 1 : offset,
            .severity  = text::Buffer::Diagnostic::Severity::Warning, // a style suggestion, not an error
            .origin    = text::Buffer::Diagnostic::Origin::Code,
            .message   = "\"" + v.violation.name + "\" is not " + CaseConventionName(v.violation.expectedConvention),
        });
    }
    if (violations.empty()) {
        appendLine("No violations -- every checked entity kind conforms to its configured convention.");
    }

    buffer->SetPoint(diagnostics.empty() ? 0 : diagnostics.front().startByte);
    buffer->EndUndoGroup();
    buffer->SetReadOnly(true);
    buffer->SetDiagnostics(std::move(diagnostics));
    return *buffer;
}

} // namespace ned::editor
