#include "UI/BufferView/GutterModel.h"

#include <algorithm>
#include <unordered_map>

#include "Text/ITextStorage.h"
#include "UI/ActiveBuffer.h"

namespace ned::ui::bufferview {

namespace {

    // Error > Warning > Information > Hint, so the most severe diagnostic
    // starting on a line is the one the gutter marks.
    int SeverityRank(text::Buffer::Diagnostic::Severity severity) {
        switch (severity) {
            case text::Buffer::Diagnostic::Severity::Error:
                return 4;
            case text::Buffer::Diagnostic::Severity::Warning:
                return 3;
            case text::Buffer::Diagnostic::Severity::Information:
                return 2;
            case text::Buffer::Diagnostic::Severity::Hint:
                return 1;
        }
        return 0;
    }

} // namespace

const std::vector<std::pair<std::size_t, std::size_t>>& GutterModel::UnsavedChangeLineRanges() const {
    EnsureUnsavedChanges();
    return unsavedChangeLineRanges_;
}

void GutterModel::EnsureUnsavedChanges() const {
    text::Buffer& buffer = context_.activeBuffer.Get();

    const CacheStamp stamp = CacheStamp::For(&buffer, {buffer.ContentGeneration(), buffer.UnsavedChangeGeneration()});
    if (unsavedChangeStamp_.Matches(stamp)) {
        return;
    }

    unsavedChangeLineRanges_.clear();
    const text::ITextStorage& content = buffer.Content();
    for (const auto& [byteStart, byteEnd] : buffer.UnsavedChangeRanges()) {
        const std::size_t startLine = content.ByteOffsetToLine(byteStart);
        // byteEnd is exclusive and may sit exactly on a line boundary (the byte
        // after the range's own last one) -- back it up by one before
        // converting, or a range ending at "line N+1, column 0" would be
        // counted as touching line N+1 too.
        const std::size_t endLine = content.ByteOffsetToLine(byteEnd > byteStart ? byteEnd - 1 : byteStart);
        // UnsavedChangeRanges() arrives sorted by byte offset, so startLine is
        // never less than the previous entry's -- merge with the last pushed
        // range when they touch or overlap, the same "already sorted, just
        // coalesce adjacent" approach Buffer::MergeUnsavedRange itself uses.
        if (!unsavedChangeLineRanges_.empty() && startLine <= unsavedChangeLineRanges_.back().second) {
            unsavedChangeLineRanges_.back().second = std::max(unsavedChangeLineRanges_.back().second, endLine + 1);
        }
        else {
            unsavedChangeLineRanges_.emplace_back(startLine, endLine + 1);
        }
    }

    unsavedChangeStamp_ = stamp;
}

const std::vector<std::pair<std::size_t, text::Buffer::Diagnostic::Severity>>& GutterModel::DiagnosticLineSeverities()
    const {
    EnsureDiagnosticSeverities();
    return diagnosticLineSeverities_;
}

void GutterModel::EnsureDiagnosticSeverities() const {
    text::Buffer& buffer = context_.activeBuffer.Get();

    const CacheStamp stamp = CacheStamp::For(&buffer, {buffer.DiagnosticsGeneration()});
    if (diagnosticStamp_.Matches(stamp)) {
        return;
    }

    diagnosticLineSeverities_.clear();
    const text::ITextStorage& content = buffer.Content();
    // Diagnostics() arrives in whatever order the server reported, not sorted
    // by position -- collapse to at most one {line, severity} entry per line
    // through a local map, then sort once for the per-row lower_bound the
    // renderer does.
    std::unordered_map<std::size_t, text::Buffer::Diagnostic::Severity> mostSevereByLine;
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        const std::size_t line = content.ByteOffsetToLine(diagnostic.startByte);
        const auto        it   = mostSevereByLine.find(line);
        if (it == mostSevereByLine.end() || SeverityRank(diagnostic.severity) > SeverityRank(it->second)) {
            mostSevereByLine[line] = diagnostic.severity;
        }
    }
    diagnosticLineSeverities_.assign(mostSevereByLine.begin(), mostSevereByLine.end());
    std::sort(diagnosticLineSeverities_.begin(), diagnosticLineSeverities_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    diagnosticStamp_ = stamp;
}

const std::vector<text::ConflictHunk>& GutterModel::ConflictHunks() const {
    EnsureConflictHunks();
    return conflictHunks_;
}

void GutterModel::EnsureConflictHunks() const {
    text::Buffer& buffer = context_.activeBuffer.Get();

    const CacheStamp stamp = CacheStamp::For(&buffer, {buffer.ContentGeneration()});
    if (conflictHunkStamp_.Matches(stamp)) {
        return;
    }

    conflictHunks_     = text::ParseConflictHunks(buffer.Text());
    conflictHunkStamp_ = stamp;
}

} // namespace ned::ui::bufferview
