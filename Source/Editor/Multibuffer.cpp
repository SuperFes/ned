#include "Multibuffer.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "MultibufferFoldSettings.h"
#include "MultibufferLimits.h"
#include "Project/Undo.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/FilePreservation.h"

namespace ned::editor::multibuffer {

void MultibufferIndex::SetSpans(std::vector<ExcerptSpan> spans) {
    std::sort(spans.begin(), spans.end(),
              [](const ExcerptSpan& a, const ExcerptSpan& b) { return a.compositeStartByte < b.compositeStartByte; });
    spans_ = std::move(spans);
}

const ExcerptSpan* MultibufferIndex::SpanAtOffset(std::size_t compositeByteOffset) const {
    for (const ExcerptSpan& span : spans_) {
        if (compositeByteOffset >= span.compositeStartByte && compositeByteOffset < span.compositeEndByte) {
            return &span;
        }
    }
    return nullptr;
}

const std::vector<ExcerptSpan>& MultibufferIndex::Spans() const {
    return spans_;
}

void MultibufferIndex::SetLineTints(std::vector<std::pair<std::size_t, LineTint>> tints) {
    std::sort(tints.begin(), tints.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    lineTints_ = std::move(tints);
}

LineTint MultibufferIndex::TintForLine(std::size_t compositeLine) const {
    const auto it = std::lower_bound(lineTints_.begin(), lineTints_.end(), compositeLine,
                                     [](const auto& entry, std::size_t line) { return entry.first < line; });
    return (it != lineTints_.end() && it->first == compositeLine) ? it->second : LineTint::None;
}

namespace {

    // Buffer* identity -> its MultibufferIndex. See this header's own doc
    // comment for why this isn't mutex-guarded the way the process-wide
    // settings modules elsewhere in Editor/ are.
    std::unordered_map<const text::Buffer*, MultibufferIndex>& Registry() {
        static std::unordered_map<const text::Buffer*, MultibufferIndex> registry;
        return registry;
    }

} // namespace

MultibufferIndex* MultibufferIndexFor(const text::Buffer& buffer) {
    auto&      registry = Registry();
    const auto it       = registry.find(&buffer);
    return it == registry.end() ? nullptr : &it->second;
}

void SetMultibufferIndexFor(text::Buffer& buffer, MultibufferIndex index) {
    Registry()[&buffer] = std::move(index);
}

void ClearMultibufferIndexFor(const text::Buffer& buffer) {
    Registry().erase(&buffer);
}

void ClearRegistryForTesting() {
    Registry().clear();
}

std::vector<std::pair<std::size_t, std::size_t>> FoldableExcerptBlocks(const MultibufferIndex& index) {
    std::vector<std::pair<std::size_t, std::size_t>> blocks;
    blocks.reserve(index.Spans().size());
    for (const ExcerptSpan& span : index.Spans()) {
        if (span.bodyStartByte != span.compositeStartByte) {
            blocks.emplace_back(span.compositeStartByte, span.compositeEndByte);
        }
    }
    return blocks;
}

namespace {

    // [startLine, endLine] (1-indexed, inclusive) as a byte range into
    // fullText, scanning newlines -- a plain linear scan, not Rope-backed,
    // since every caller runs this once per excerpt at build time, not per
    // frame. nullopt if startLine is past fullText's own last line. Used
    // only for the "no live buffer, read the file fresh off disk" fallback
    // below -- an already-open buffer resolves through the bounded
    // ITextStorage overload just below instead.
    std::optional<std::pair<std::size_t, std::size_t>> LineRangeToByteRange(const std::string& fullText,
                                                                            std::size_t startLine, std::size_t endLine) {
        std::size_t line       = 1;
        std::size_t pos        = 0;
        std::size_t sliceStart = std::string::npos;
        while (pos <= fullText.size()) {
            if (line == startLine) {
                sliceStart = pos;
            }
            if (line == endLine + 1 || pos == fullText.size()) {
                if (sliceStart == std::string::npos) {
                    return std::nullopt;
                }
                return std::make_pair(sliceStart, pos);
            }
            const std::size_t next = fullText.find('\n', pos);
            pos                    = (next == std::string::npos) ? fullText.size() : next + 1;
            ++line;
        }
        return std::nullopt;
    }

    // huge-file-navigation-verification follow-up: the same [startLine,
    // endLine] contract as the std::string overload above, but resolved via
    // ITextStorage's own bounded line index (LineToByteOffset is O(log n),
    // clamping past the real end rather than throwing -- Rope::
    // LineToByteOffset/PieceTable::LineToByteOffset's own documented
    // behavior) instead of a linear newline scan over the whole document.
    // This is what lets an already-open *huge* source buffer answer an
    // excerpt request without ever materializing its full content -- a real,
    // previously-unguarded `buffer.Text()` call both callers below used to
    // make unconditionally, found while verifying multibuffer excerpts
    // against a huge source buffer.
    std::optional<std::pair<std::size_t, std::size_t>> LineRangeToByteRange(const text::ITextStorage& content,
                                                                            std::size_t startLine, std::size_t endLine) {
        const std::size_t lineCount = content.LineCount();
        if (startLine == 0 || startLine > lineCount) {
            return std::nullopt;
        }
        const std::size_t clampedEndLine = std::min(endLine, lineCount);
        return std::make_pair(content.LineToByteOffset(startLine - 1), content.LineToByteOffset(clampedEndLine));
    }

} // namespace

std::string ReadExcerptText(text::BufferList& bufferList, const std::filesystem::path& path, std::size_t startLine,
                            std::size_t endLine) {
    // Prefers a live, already-open Buffer's own content (unsaved edits show
    // up) via the bounded ITextStorage overload above; falls back to a raw
    // file read plus the linear-scan overload only when the file isn't
    // open. A plain lookup (FindByPath, not OpenOrCreateFile): resolving an
    // excerpt must not have the side effect of opening a new buffer for
    // every excerpt's source file just to build a display buffer
    // (project-find-references can span dozens of files). Degrades to ""
    // on any read failure, the same posture as everywhere else in this
    // subsystem.
    if (text::Buffer* open = bufferList.FindByPath(path)) {
        const auto range = LineRangeToByteRange(open->Content(), startLine, endLine);
        return range ? open->Content().Substring(range->first, range->second - range->first) : std::string();
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string fullText = contents.str();
    const auto        range    = LineRangeToByteRange(fullText, startLine, endLine);
    return range ? fullText.substr(range->first, range->second - range->first) : std::string();
}

namespace {

    // A fixed-width box-drawing rule -- baked as literal buffer content
    // (this is a static, read-only view, not something that reflows with
    // the viewport) framing every excerpt's own title line ("ASCII
    // outline" follow-up: bold titles alone were hard to pick out of a
    // long scroll of hunks). U+2500 is the same light-horizontal glyph
    // family Border.h's own box-drawing already uses elsewhere in the UI.
    constexpr int         kRuleWidth = 78;
    constexpr const char* kRuleGlyph = "─";

    std::string MakeRuleLine() {
        std::string rule;
        rule.reserve(static_cast<std::size_t>(kRuleWidth) * 3);
        for (int i = 0; i < kRuleWidth; ++i) {
            rule += kRuleGlyph;
        }
        return rule;
    }

} // namespace

text::Buffer& BuildMultibuffer(text::BufferList& bufferList, const std::string& name,
                               const std::vector<ExcerptSource>& excerpts, std::size_t totalAvailable) {
    std::string                                   composite;
    std::vector<ExcerptSpan>                      spans;
    std::vector<std::pair<std::size_t, LineTint>> lineTints;
    std::vector<text::Buffer::ExcerptRange>       excerptRanges; // editable-multibuffer follow-up
    spans.reserve(excerpts.size());

    // Auto-collapse-on-build follow-up: header-line startBytes to mark
    // Buffer::FoldMarker::Collapsed once the composite Buffer exists below
    // (it doesn't yet -- results isn't created until after this loop) --
    // see BuildMultibuffer's own doc comment for the policy these
    // thresholds implement.
    std::vector<std::size_t> collapseOffsets;
    const std::size_t        lineThreshold = editor::MultibufferAutoCollapseLineThreshold();
    const std::size_t        byteThreshold = editor::MultibufferAutoCollapseByteThreshold();
    const std::size_t        excerptCap    = editor::MultibufferAutoCollapseExcerptCap();

    // 0-indexed, matching Rope::ByteOffsetToLine's own convention -- the
    // running composite line number as text is appended, so each body
    // line's LineTint can be recorded against the exact line it lands on
    // without a second pass over the finished text.
    std::size_t compositeLine = 0;

    // Multibuffer-gaps follow-up: the hard bound on how many excerpts are
    // stitched at all, distinct from the auto-collapse cap just above (which
    // only changes an excerpt's initial fold state -- see
    // Editor/MultibufferLimits.h's own doc comment for the difference).
    const std::size_t maxExcerpts = editor::MultibufferMaxExcerpts();
    const std::size_t keptCount   = (maxExcerpts == 0) ? excerpts.size() : std::min(excerpts.size(), maxExcerpts);
    // A caller that capped its own excerpt-building work reports the real
    // total via totalAvailable; everyone else passes 0 and the set it handed
    // over *is* the total. max(), not a plain choice between them, so a
    // caller passing a stale/too-small total can never make the note claim
    // fewer were dropped than this function itself dropped.
    const std::size_t elidedCount = std::max(totalAvailable, excerpts.size()) - keptCount;

    std::size_t excerptOrdinal = 0; // 1-based, for MultibufferAutoCollapseExcerptCap()
    for (std::size_t excerptIndex = 0; excerptIndex < keptCount; ++excerptIndex) {
        const ExcerptSource& excerpt = excerpts[excerptIndex];
        ++excerptOrdinal;
        // A rule line ahead of every excerpt (including the first) --
        // doubles as the separator from whatever came before, and gives
        // each excerpt's own title line a visible top edge. Outside every
        // span (spanStart is captured after it), so clicking it is a
        // no-op the same way the old blank separator line already was.
        composite += MakeRuleLine();
        composite += '\n';
        lineTints.emplace_back(compositeLine, LineTint::Rule);
        ++compositeLine;

        const std::size_t spanStart = composite.size();
        if (!excerpt.headerText.empty()) {
            composite += excerpt.headerText;
            composite += '\n';
            lineTints.emplace_back(compositeLine, LineTint::Header);
            ++compositeLine;
        }
        // Editable-multibuffer follow-up: only the body is ever typable --
        // captured here, before the body-lines loop below appends anything,
        // so it excludes this excerpt's own header line the same way
        // RequestDiagnosticsBuffer's own composite-offset translation
        // already does ("span start + header length + its newline").
        const std::size_t bodyStart = composite.size();

        // Body lines are appended one at a time (rather than the whole
        // string in one shot) specifically to pair each with its own
        // composite line number for lineTints -- see this loop's role in
        // the header comment above.
        std::size_t bodyLineIndex = 0;
        std::size_t bodyPos       = 0;
        while (bodyPos < excerpt.bodyText.size()) {
            const std::size_t eol     = excerpt.bodyText.find('\n', bodyPos);
            const std::size_t lineEnd = (eol == std::string::npos) ? excerpt.bodyText.size() : eol;
            composite.append(excerpt.bodyText, bodyPos, lineEnd - bodyPos);
            composite += '\n';
            if (bodyLineIndex < excerpt.lineTints.size() && excerpt.lineTints[bodyLineIndex] != LineTint::None) {
                lineTints.emplace_back(compositeLine, excerpt.lineTints[bodyLineIndex]);
            }
            ++compositeLine;
            ++bodyLineIndex;
            bodyPos = (eol == std::string::npos) ? excerpt.bodyText.size() : eol + 1;
        }

        if (!composite.empty() && composite.back() != '\n') {
            composite += '\n';
        }
        const std::size_t spanEnd = composite.size();
        spans.push_back(
            ExcerptSpan{excerpt.sourcePath, excerpt.sourceStartLine, excerpt.sourceEndLine, spanStart, spanEnd, bodyStart});

        // Auto-collapse-on-build follow-up: only an excerpt with its own
        // header line is ever offered -- one with none (bodyStart == spanStart)
        // would have nothing left visible to mark that a fold exists if
        // collapsed. Any one of the three thresholds is enough on its own.
        if (bodyStart != spanStart &&
            (bodyLineIndex > lineThreshold || excerpt.bodyText.size() > byteThreshold || excerptOrdinal > excerptCap)) {
            collapseOffsets.push_back(spanStart);
        }

        // Editable-multibuffer follow-up: resolve this excerpt's byte-exact
        // source range and register it as an ExcerptRange, so a later edit
        // in [bodyStart, spanEnd) has somewhere real to commit back to.
        // sourceStartLine == 0 ("no source line applies," e.g. a pure-
        // deletion diff hunk) or a failed line lookup (source vanished/
        // shrank since the caller counted lines) both silently fall back to
        // non-editable -- the same degrade-don't-crash posture
        // ReadExcerptText already takes toward a missing/changed source.
        if (excerpt.editable && excerpt.sourceStartLine > 0) {
            // huge-file-navigation-verification follow-up: resolves via the
            // bounded ITextStorage overload when the source is already
            // open, same as ReadExcerptText above -- this path only ever
            // needs the byte range itself (composite.substr below supplies
            // originalText), so unlike ReadExcerptText there's no source
            // text to slice out at all, live-buffer or disk.
            std::optional<std::pair<std::size_t, std::size_t>> range;
            if (text::Buffer* open = bufferList.FindByPath(excerpt.sourcePath)) {
                range = LineRangeToByteRange(open->Content(), excerpt.sourceStartLine, excerpt.sourceEndLine);
            }
            else if (std::ifstream input(excerpt.sourcePath, std::ios::binary); input) {
                std::ostringstream contents;
                contents << input.rdbuf();
                range = LineRangeToByteRange(contents.str(), excerpt.sourceStartLine, excerpt.sourceEndLine);
            }
            if (range) {
                excerptRanges.push_back(text::Buffer::ExcerptRange{
                    bodyStart, spanEnd, excerpt.sourcePath, range->first, range->second,
                    /*editable=*/true, composite.substr(bodyStart, spanEnd - bodyStart)});
            }
        }

        // A blank line of breathing room between this excerpt's own body
        // and the next rule (or the closing rule after the last excerpt) --
        // outside the span, same as the rule line itself, so it's a no-op
        // to click on.
        composite += '\n';
        ++compositeLine;
    }

    if (keptCount > 0) {
        // A closing rule so the last excerpt gets a visible bottom edge
        // too, matching every other excerpt's own top-rule framing.
        composite += MakeRuleLine();
        composite += '\n';
        lineTints.emplace_back(compositeLine, LineTint::Rule);
        ++compositeLine;
    }

    // Multibuffer-gaps follow-up: never a silent truncation. Outside every
    // ExcerptSpan (spans were closed above), so Enter/click on it is the
    // same no-op a rule line already is, and named in the buffer's own text
    // rather than only in a status message that the next keystroke clears.
    if (elidedCount > 0) {
        composite += "… " + std::to_string(elidedCount) + " more not shown (ned/set-multibuffer-max-excerpts)";
        composite += '\n';
        lineTints.emplace_back(compositeLine, LineTint::Header);
        ++compositeLine;
    }

    text::Buffer& results = bufferList.CreateBuffer(name);
    results.InsertAtPoint(composite);
    results.SetPoint(0);
    // Auto-collapse-on-build follow-up: an ordinary FoldMarker per offset
    // collected above -- the composite Buffer has to exist first, which is
    // why this couldn't happen inside the loop itself. From here on this is
    // exactly like any other fold (code-fold-toggle/unfold-all included);
    // nothing distinguishes an auto-collapsed excerpt from a manually
    // collapsed one.
    for (const std::size_t offset : collapseOffsets) {
        results.SetFoldMarker(offset, text::Buffer::FoldMarker::Collapsed);
    }
    if (excerptRanges.empty()) {
        // read-only-buffers follow-up: same reasoning as BuildResultsBuffer's
        // own doc comment -- a synthesized, no-file-to-save-to buffer.
        results.SetReadOnly(true);
    }
    else {
        // Editable-multibuffer follow-up: chrome (headers/rules/blank
        // separators) stays protected via Buffer's own point-level
        // enforcement (CanInsertAtExcerpt/CanDeleteExcerptRange) now that
        // ExcerptRanges_ is non-empty -- ReadOnly() itself only needs to
        // gate "can this buffer be edited at all," not the chrome/body
        // split.
        results.SetExcerptRanges(std::move(excerptRanges));
    }

    MultibufferIndex index;
    index.SetSpans(std::move(spans));
    index.SetLineTints(std::move(lineTints));
    SetMultibufferIndexFor(results, std::move(index));

    return results;
}

namespace {

    // The editable range covering offset, or nullptr. Edge semantics match
    // CanInsertAtExcerpt's: [start, end) -- an offset sitting exactly at a
    // range's end belongs to the chrome after it, not to the excerpt.
    const text::Buffer::ExcerptRange* EditableRangeAt(const text::Buffer& composite, std::size_t offset) {
        for (const text::Buffer::ExcerptRange& range : composite.ExcerptRanges()) {
            if (range.editable && offset >= range.start && offset < range.end) {
                return &range;
            }
        }
        return nullptr;
    }

    // Restores one range's originalText in place. Assumes the caller has
    // already opened an undo group if it wants several of these to fold into
    // one step. Returns false when the text already matches.
    bool RevertRange(text::Buffer& composite, std::size_t start, std::size_t end, const std::string& originalText) {
        if (composite.Content().Substring(start, end - start) == originalText) {
            return false;
        }
        composite.DeleteRange(start, end - start);
        composite.InsertAt(start, originalText);
        return true;
    }

} // namespace

std::optional<std::filesystem::path> ExcerptPathAtOffset(const text::Buffer& composite, std::size_t compositeByteOffset) {
    const text::Buffer::ExcerptRange* range = EditableRangeAt(composite, compositeByteOffset);
    return range ? std::optional<std::filesystem::path>(range->sourcePath) : std::nullopt;
}

bool RevertExcerptAtOffset(text::Buffer& composite, std::size_t compositeByteOffset) {
    const text::Buffer::ExcerptRange* range = EditableRangeAt(composite, compositeByteOffset);
    if (range == nullptr) {
        return false;
    }
    // Copied out before the edit: reverting relocates the vector these live in.
    const std::size_t start        = range->start;
    const std::size_t end          = range->end;
    const std::string originalText = range->originalText;

    composite.BeginUndoGroup();
    const bool changed = RevertRange(composite, start, end, originalText);
    composite.EndUndoGroup();
    return changed;
}

std::size_t RevertExcerptsForFileAtOffset(text::Buffer& composite, std::size_t compositeByteOffset) {
    const text::Buffer::ExcerptRange* under = EditableRangeAt(composite, compositeByteOffset);
    if (under == nullptr) {
        return 0;
    }
    const std::filesystem::path path = under->sourcePath;

    // Reverse order, and each range's fields re-read fresh: an earlier
    // excerpt's offsets can't be disturbed by a later one's revert, and the
    // vector is relocated by every edit.
    std::size_t reverted = 0;
    composite.BeginUndoGroup();
    for (std::size_t i = composite.ExcerptRanges().size(); i-- > 0;) {
        const text::Buffer::ExcerptRange& range = composite.ExcerptRanges()[i];
        if (!range.editable || range.sourcePath != path) {
            continue;
        }
        const std::size_t start        = range.start;
        const std::size_t end          = range.end;
        const std::string originalText = range.originalText;
        if (RevertRange(composite, start, end, originalText)) {
            ++reverted;
        }
    }
    composite.EndUndoGroup();
    return reverted;
}

std::optional<std::size_t> NextExcerptBodyStart(const text::Buffer& composite, std::size_t compositeByteOffset) {
    const MultibufferIndex* index = MultibufferIndexFor(composite);
    if (index == nullptr) {
        return std::nullopt;
    }
    for (const ExcerptSpan& span : index->Spans()) { // sorted by compositeStartByte
        if (span.bodyStartByte > compositeByteOffset) {
            return span.bodyStartByte;
        }
    }
    return std::nullopt;
}

namespace {

    // ExcerptBodyRanges/ExcerptSourcePaths' shared "which set describes this
    // buffer's excerpts right now" step -- see ExcerptBodyRanges' own doc
    // comment for the rule.
    std::vector<std::pair<std::size_t, std::size_t>> BodyRangesFromExcerptRanges(const text::Buffer& composite) {
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        for (const text::Buffer::ExcerptRange& range : composite.ExcerptRanges()) {
            ranges.emplace_back(range.start, range.end);
        }
        return ranges;
    }

} // namespace

std::vector<std::pair<std::size_t, std::size_t>> ExcerptBodyRanges(const text::Buffer& composite) {
    std::vector<std::pair<std::size_t, std::size_t>> ranges = BodyRangesFromExcerptRanges(composite);
    if (ranges.empty()) {
        if (const MultibufferIndex* index = MultibufferIndexFor(composite)) {
            for (const ExcerptSpan& span : index->Spans()) {
                ranges.emplace_back(span.bodyStartByte, span.compositeEndByte);
            }
        }
    }

    // A degenerate range (an excerpt deleted back to nothing, which
    // ExcerptRange deliberately keeps rather than drops) contains no bytes to
    // search, so it isn't a scope -- dropping it here keeps every caller's
    // containment test a plain start <= x && y <= end with no empty-range
    // special case of its own.
    std::erase_if(ranges, [](const std::pair<std::size_t, std::size_t>& range) { return range.first >= range.second; });
    std::sort(ranges.begin(), ranges.end());
    return ranges;
}

std::vector<std::filesystem::path> ExcerptSourcePaths(const text::Buffer& composite) {
    std::vector<std::filesystem::path> paths;
    auto                               append = [&paths](const std::filesystem::path& path) {
        if (path.empty() || std::find(paths.begin(), paths.end(), path) != paths.end()) {
            return;
        }
        paths.push_back(path);
    };

    if (!composite.ExcerptRanges().empty()) {
        for (const text::Buffer::ExcerptRange& range : composite.ExcerptRanges()) {
            append(range.sourcePath);
        }
        return paths;
    }
    if (const MultibufferIndex* index = MultibufferIndexFor(composite)) {
        for (const ExcerptSpan& span : index->Spans()) {
            append(span.sourcePath);
        }
    }
    return paths;
}

std::optional<std::size_t> PreviousExcerptBodyStart(const text::Buffer& composite, std::size_t compositeByteOffset) {
    const MultibufferIndex* index = MultibufferIndexFor(composite);
    if (index == nullptr) {
        return std::nullopt;
    }
    std::optional<std::size_t> best;
    for (const ExcerptSpan& span : index->Spans()) {
        if (span.bodyStartByte < compositeByteOffset) {
            best = span.bodyStartByte;
        }
    }
    return best;
}

namespace {

    // The atomic sibling-then-rename write Buffer::SaveToFile and
    // ProjectReplace's own rewrite both use -- see Text/FilePreservation.h
    // for what a bare rename would silently discard (mode bits, xattrs/ACLs,
    // the symlink the path was reached through, extra hard links). Returns
    // false on any failure, leaving the original file untouched.
    bool WriteFileAtomically(const std::filesystem::path& path, const std::string& content) {
        const std::filesystem::path         target     = text::ResolveSaveTarget(path);
        const text::PreservedFileAttributes attributes = text::CaptureFileAttributes(target);

        if (text::ShouldWriteInPlace(attributes)) {
            std::ofstream output(target, std::ios::binary | std::ios::trunc);
            if (!output) {
                return false;
            }
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            return static_cast<bool>(output);
        }

        std::filesystem::path tempPath = target;
        tempPath += ".ned-tmp";
        {
            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                return false;
            }
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!output) {
                return false;
            }
        }
        text::ApplyFileAttributes(tempPath, attributes);

        std::error_code ec;
        std::filesystem::rename(tempPath, target, ec);
        return !ec; // a failed rename leaves the .ned-tmp behind -- rare, and better than losing the original
    }

    std::optional<std::string> ReadWholeFile(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return std::nullopt;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

} // namespace

CommitResult CommitExcerptChanges(text::BufferList& bufferList, text::Buffer& composite, ProjectUndoManager* projectUndo,
                                  const std::filesystem::path* onlyPath, CommitTarget target) {
    CommitResult result;
    // One record per source buffer actually written -- see this function's
    // own doc comment.
    ProjectEditTransaction transaction;

    // Grouped by source path -- ranges belonging to a different source file
    // don't interact, and within one file's own group they're applied in
    // descending source-byte order below (ApplyWorkspaceTextEdits's own
    // precedent in BufferView.cpp: keeps a not-yet-applied edit's stored
    // offset valid as an earlier, lower-offset edit shifts nothing above
    // it). Only ranges whose current composite text actually differs from
    // their originalText snapshot are collected -- an untouched excerpt is
    // left alone, not rewritten with identical content.
    std::map<std::filesystem::path, std::vector<std::size_t>> changedByPath;
    const std::vector<text::Buffer::ExcerptRange>&            ranges = composite.ExcerptRanges();
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const text::Buffer::ExcerptRange& range = ranges[i];
        if (!range.editable) {
            continue;
        }
        if (onlyPath != nullptr && range.sourcePath != *onlyPath) {
            continue; // left pending on purpose -- see this function's own doc comment
        }
        const std::string currentText = composite.Content().Substring(range.start, range.end - range.start);
        if (currentText != range.originalText) {
            changedByPath[range.sourcePath].push_back(i);
        }
    }

    for (auto& [path, indices] : changedByPath) {
        // Disk target, and no unsaved buffer standing in the way: rewrite the
        // file itself and never open a buffer for it. A file whose buffer is
        // open *and modified* deliberately falls through to the LiveBuffers
        // path below instead -- see CommitTarget's own doc comment.
        text::Buffer* const openBuffer = bufferList.FindByPath(path);
        // A huge source is never read whole here either -- it takes the
        // LiveBuffers path below, where Buffer's own storage applies the edit
        // in place. Huge-file support stays second-class throughout this
        // subsystem: correct, bounded, and never the thing that shapes the
        // fast path.
        std::error_code      hugeCheckError;
        const std::uintmax_t sourceSize = std::filesystem::file_size(path, hugeCheckError);
        const bool           hugeSource = !hugeCheckError && sourceSize > text::HugeFileThreshold();

        if (target == CommitTarget::Disk && !hugeSource && (openBuffer == nullptr || !openBuffer->Modified())) {
            std::optional<std::string> fileText = ReadWholeFile(path);
            if (!fileText) {
                result.skipped.emplace_back(path, "source file could not be read");
                continue;
            }

            // Same two guards the buffer path applies, against the file's own
            // bytes: the excerpt's recorded range must still exist and still
            // hold the text it was snapshotted with.
            std::sort(indices.begin(), indices.end(),
                      [&ranges](std::size_t a, std::size_t b) { return ranges[a].sourceStartByte > ranges[b].sourceStartByte; });
            bool conflicted = false;
            for (std::size_t i : indices) {
                const text::Buffer::ExcerptRange& range = ranges[i];
                if (range.sourceEndByte > fileText->size() ||
                    fileText->compare(range.sourceStartByte, range.sourceEndByte - range.sourceStartByte, range.originalText) != 0) {
                    conflicted = true;
                    break;
                }
            }
            if (conflicted) {
                result.skipped.emplace_back(path, "source file changed on disk since this multibuffer was built");
                continue;
            }

            for (std::size_t i : indices) {
                const text::Buffer::ExcerptRange& range   = ranges[i];
                const std::string                 newText = composite.Content().Substring(range.start, range.end - range.start);
                fileText->replace(range.sourceStartByte, range.sourceEndByte - range.sourceStartByte, newText);
                composite.MarkExcerptRangeCommitted(range.start, range.end, newText, range.sourceStartByte,
                                                    range.sourceStartByte + newText.size());
                ++result.committedExcerpts;
            }
            if (!WriteFileAtomically(path, *fileText)) {
                result.skipped.emplace_back(path, "source file could not be written");
                continue;
            }
            ++result.filesWritten;
            if (openBuffer != nullptr) {
                openBuffer->Revert(); // unmodified by the check above -- show what's now on disk
            }
            continue;
        }

        text::Buffer* source = nullptr;
        try {
            source = &bufferList.OpenOrCreateFile(path);
        }
        catch (const std::runtime_error& e) {
            result.skipped.emplace_back(path, e.what());
            continue;
        }

        // Disk-level conflict guard -- same posture save-buffer's own
        // ConfirmOverwriteSave check takes toward a file that changed out
        // from under an open buffer, except a commit has no interactive
        // prompt to fall back to, so it just skips this file's whole batch
        // with a warning rather than risking a silent overwrite.
        if (source->ExternallyModified()) {
            result.skipped.emplace_back(path, "source file changed on disk since this multibuffer was built");
            continue;
        }

        // In-memory conflict guard -- catches a change to this exact byte
        // range since the excerpt was snapshotted that never touched disk
        // (another edit to the same open buffer, an AutoMerge resolution,
        // ...), which ExternallyModified() alone can't see. Deliberately a
        // direct comparison against the live range's own bytes rather than
        // a whole-buffer ContentGeneration() snapshot: a generation counter
        // would trip on any unrelated edit anywhere else in the same
        // source buffer, over-conservative for excerpts that never
        // overlap.
        std::sort(indices.begin(), indices.end(),
                  [&ranges](std::size_t a, std::size_t b) { return ranges[a].sourceStartByte > ranges[b].sourceStartByte; });

        const std::size_t sourceLength = source->Content().ByteLength();
        bool              conflicted   = false;
        for (std::size_t i : indices) {
            const text::Buffer::ExcerptRange& range = ranges[i];
            if (range.sourceEndByte > sourceLength) {
                conflicted = true;
                break;
            }
            const std::string liveSourceText =
                source->Content().Substring(range.sourceStartByte, range.sourceEndByte - range.sourceStartByte);
            if (liveSourceText != range.originalText) {
                conflicted = true;
                break;
            }
        }
        if (conflicted) {
            result.skipped.emplace_back(path, "source buffer changed since this multibuffer was built");
            continue;
        }

        const std::size_t beforeSequence = source->CurrentUndoSequence();
        source->BeginUndoGroup();
        for (std::size_t i : indices) {
            const text::Buffer::ExcerptRange& range   = ranges[i];
            const std::string                 newText = composite.Content().Substring(range.start, range.end - range.start);
            source->DeleteRange(range.sourceStartByte, range.sourceEndByte - range.sourceStartByte);
            source->InsertAt(range.sourceStartByte, newText);
            composite.MarkExcerptRangeCommitted(range.start, range.end, newText, range.sourceStartByte,
                                                range.sourceStartByte + newText.size());
            ++result.committedExcerpts;
        }
        source->EndUndoGroup();
        ++result.buffersCommitted;
        transaction.records.push_back(ProjectUndoRecord{path, beforeSequence, source->CurrentUndoSequence()});
    }

    if (projectUndo != nullptr && transaction.records.size() > 1) {
        transaction.description = "Commit " + std::to_string(result.committedExcerpts) + " excerpt" +
                                  (result.committedExcerpts == 1 ? "" : "s") + " (" +
                                  std::to_string(transaction.records.size()) + " files)";
        projectUndo->RecordTransaction(std::move(transaction));
    }

    return result;
}

} // namespace ned::editor::multibuffer
