#include "ScopedFormat.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "FinalNewline.h"
#include "FormatEdit.h"
#include "FormatPasses.h"
#include "Indent.h"
#include "TrimOnSave.h"

namespace ned::editor {

namespace {

    using LineRange = std::pair<std::size_t, std::size_t>; // [startLine, endLineExclusive)

    // Buffer::UnsavedChangeRanges() is already sorted/merged by byte
    // overlap, but two non-overlapping byte ranges can still snap to the
    // same or adjacent lines -- e.g. two edits on the same line, or on
    // consecutive lines -- so the merge has to run again after snapping.
    std::vector<LineRange> SnappedLineRanges(const text::Buffer& buffer) {
        std::vector<LineRange> ranges;
        const auto&            content = buffer.Content();
        for (const auto& [start, end] : buffer.UnsavedChangeRanges()) {
            const std::size_t startLine = content.ByteOffsetToLine(start);
            // end is exclusive; a zero-length range (start == end) still
            // names the line it sits on.
            const std::size_t lastTouchedByte  = (end > start) ? end - 1 : start;
            const std::size_t endLineExclusive = content.ByteOffsetToLine(lastTouchedByte) + 1;
            if (!ranges.empty() && startLine <= ranges.back().second) {
                ranges.back().second = std::max(ranges.back().second, endLineExclusive);
            }
            else {
                ranges.emplace_back(startLine, endLineExclusive);
            }
        }
        return ranges;
    }

    // Trailing whitespace strip, scoped to [startLine, endLineExclusive) --
    // Text/WhitespaceHygiene.h's TrimTrailingWhitespaceAndBlankLines is
    // whole-document only, so this is a small from-scratch, per-line
    // equivalent of just its trim half (the blank-line-collapse half is
    // deliberately not attempted scoped at all -- see this file's own
    // header comment).
    std::vector<FormatTextEdit> ScopedTrimEdits(std::string_view text, const text::Buffer& buffer, std::size_t startLine,
                                                std::size_t endLineExclusive) {
        std::vector<FormatTextEdit> edits;
        if (!TrimTrailingWhitespaceOnSave()) {
            return edits;
        }
        const auto& content = buffer.Content();
        for (std::size_t line = startLine; line < endLineExclusive; ++line) {
            const std::size_t lineStart = content.LineToByteOffset(line);
            const std::size_t lineEndWithNewline =
                (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : text.size();
            std::size_t contentEnd = lineEndWithNewline;
            if (contentEnd > lineStart && contentEnd <= text.size() && text[contentEnd - 1] == '\n') {
                --contentEnd; // exclude the line's own trailing newline from the trim scan
            }
            std::size_t trimStart = contentEnd;
            while (trimStart > lineStart && (text[trimStart - 1] == ' ' || text[trimStart - 1] == '\t')) {
                --trimStart;
            }
            if (trimStart < contentEnd) {
                edits.push_back(FormatTextEdit{trimStart, contentEnd, std::string()});
            }
        }
        return edits;
    }

    // Applies edits fully contained in [scopeStart, scopeEnd), returning the
    // scope's own new end offset (its start never moves -- see this file's
    // header comment on why straddling edits are declined rather than
    // partially applied).
    std::size_t ApplyContainedEdits(text::Buffer& buffer, std::vector<FormatTextEdit> edits, std::size_t scopeStart,
                                    std::size_t scopeEnd, bool& changed) {
        std::vector<FormatTextEdit> inScope;
        std::ptrdiff_t              delta = 0;
        for (FormatTextEdit& edit : edits) {
            if (edit.start >= scopeStart && edit.end <= scopeEnd) {
                delta += static_cast<std::ptrdiff_t>(edit.text.size()) - static_cast<std::ptrdiff_t>(edit.end - edit.start);
                inScope.push_back(std::move(edit));
            }
        }
        if (!inScope.empty()) {
            ApplyFormatTextEdits(buffer, std::move(inScope));
            changed = true;
        }
        return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(scopeEnd) + delta);
    }

} // namespace

bool ApplyScopedFormatOnSave(text::Buffer& buffer, const Mode& mode) {
    if (buffer.Content().IsHuge() || buffer.UnsavedChangeRanges().empty()) {
        return false; // second-class throughout this codebase -- see ROADMAP.md's own Huge-file streaming sweep entry
    }

    const std::vector<LineRange> lineRanges = SnappedLineRanges(buffer);
    if (lineRanges.empty()) {
        return false;
    }

    const std::string languageKey = LanguageKeyForMode(mode);
    bool              changed     = false;

    buffer.BeginUndoGroup();

    // Indent first, per region, by LINE index -- stable across this step
    // (a reindent only ever rewrites a line's own leading whitespace, never
    // adds/removes a line) regardless of what an earlier region's own
    // indent already shifted in byte terms.
    if (mode.indentColumn) {
        for (const auto& [startLine, endLineExclusive] : lineRanges) {
            if (IndentRegion(buffer, mode, startLine, endLineExclusive) > 0) {
                changed = true;
            }
        }
    }

    // Every capture-driven pass (Editor/FormatPasses.h -- the one list
    // format-buffer and `ned --format` also run, so this path can no longer
    // fall behind them) and then the scoped trim, per region. Byte-range
    // based from here on, since Wrap/Break can add or remove lines. Re-derived fresh per
    // region from the (still-stable) line indices, since Indent above may
    // have shifted byte offsets within earlier regions' own lines.
    for (const auto& [startLine, endLineExclusive] : lineRanges) {
        const text::ITextStorage& contentBeforeRegion = buffer.Content();
        std::size_t               scopeStart          = contentBeforeRegion.LineToByteOffset(startLine);
        std::size_t               scopeEnd            = (endLineExclusive < contentBeforeRegion.LineCount())
                                                            ? contentBeforeRegion.LineToByteOffset(endLineExclusive)
                                                            : contentBeforeRegion.ByteLength();

        // align/arrange/rewrite-kind follow-up: same pass order as
        // format-buffer's own whole-buffer chain (Commands.cpp's own header
        // comment on that chain has the full reasoning) -- Rewrite, then
        // Arrange, then Blank/Wrap/Break/Space, then Align last. Arrange and
        // Align both compute over a capture GROUP that can span lines
        // outside this region; ApplyContainedEdits' own containment check
        // is what keeps that safe here -- a group-spanning edit that
        // straddles the scope boundary is declined outright (not
        // approximated), the same as every other rule kind's own scoped
        // edit already is, and still applies in full via an explicit
        // format-buffer.
        if (mode.formatCaptures) {
            for (const FormatPass& pass : NativeFormatPasses()) {
                const std::string text = buffer.Text();
                scopeEnd               = ApplyContainedEdits(buffer, pass.compute(text, languageKey, mode.formatCaptures(text)),
                                                             scopeStart, scopeEnd, changed);
            }
        }

        // Scoped Hygiene: trim per-line, computed fresh against the
        // now-final text for this region (line count for this region is
        // stable once Wrap/Break/Space are done, but re-deriving the line
        // range from byte offsets each time would be circular -- instead
        // this recomputes endLine from the CURRENT scopeEnd byte offset,
        // which is still exact since scopeEnd was relocated through every
        // edit applied above).
        {
            const std::string         textBeforeTrim = buffer.Text();
            const text::ITextStorage& content        = buffer.Content();
            const std::size_t         currentEndLine = content.ByteOffsetToLine(scopeEnd > scopeStart ? scopeEnd - 1 : scopeEnd) + 1;
            scopeEnd                                 = ApplyContainedEdits(buffer, ScopedTrimEdits(textBeforeTrim, buffer, startLine, currentEndLine),
                                                                           scopeStart, scopeEnd, changed);

            // Final newline: only when this region's own end is genuinely
            // the buffer's last line -- FinalNewline.h's own disk-only
            // mechanism already covers every other case (and every save
            // regardless of this feature), so this only needs to handle
            // the one case a scoped save could otherwise leave the buffer
            // itself (not just the file written from it) without a
            // trailing newline the user can see. Re-read fresh -- the trim
            // step just above may have changed the buffer's own last byte.
            const std::string finalText = buffer.Text();
            if (EnsureFinalNewline() && currentEndLine >= buffer.Content().LineCount() && !finalText.empty() &&
                scopeEnd == buffer.Content().ByteLength() && finalText.back() != '\n') {
                buffer.InsertAt(scopeEnd, "\n");
                changed = true;
            }
        }
    }

    buffer.EndUndoGroup();
    return changed;
}

} // namespace ned::editor
