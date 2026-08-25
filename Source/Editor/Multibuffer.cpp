#include "Multibuffer.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "Text/Buffer.h"
#include "Text/BufferList.h"

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

namespace {

    // Prefers a live, already-open Buffer's own content (unsaved edits show
    // up), else a raw file read -- shared by ReadExcerptText and
    // BuildMultibuffer's own editable-excerpt byte-range resolution below.
    // A plain lookup (FindByPath, not OpenOrCreateFile): resolving a byte
    // range for an editable excerpt must not have the side effect of
    // opening a new buffer for every excerpt's source file just to build a
    // display buffer (project-find-references can span dozens of files).
    // Returns "" on any read failure, the same degrade-don't-crash posture
    // as everywhere else in this subsystem.
    std::string ReadFullSourceText(text::BufferList& bufferList, const std::filesystem::path& path) {
        if (text::Buffer* open = bufferList.FindByPath(path)) {
            return open->Text();
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return {};
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    // [startLine, endLine] (1-indexed, inclusive) as a byte range into
    // fullText, scanning newlines -- a plain linear scan, not Rope-backed,
    // since every caller runs this once per excerpt at build time, not per
    // frame. nullopt if startLine is past fullText's own last line.
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

} // namespace

std::string ReadExcerptText(text::BufferList& bufferList, const std::filesystem::path& path, std::size_t startLine,
                            std::size_t endLine) {
    const std::string fullText = ReadFullSourceText(bufferList, path);
    const auto         range    = LineRangeToByteRange(fullText, startLine, endLine);
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
                               const std::vector<ExcerptSource>& excerpts) {
    std::string                                   composite;
    std::vector<ExcerptSpan>                      spans;
    std::vector<std::pair<std::size_t, LineTint>> lineTints;
    std::vector<text::Buffer::ExcerptRange>       excerptRanges; // editable-multibuffer follow-up
    spans.reserve(excerpts.size());

    // 0-indexed, matching Rope::ByteOffsetToLine's own convention -- the
    // running composite line number as text is appended, so each body
    // line's LineTint can be recorded against the exact line it lands on
    // without a second pass over the finished text.
    std::size_t compositeLine = 0;

    for (const ExcerptSource& excerpt : excerpts) {
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
        spans.push_back(ExcerptSpan{excerpt.sourcePath, excerpt.sourceStartLine, excerpt.sourceEndLine, spanStart, spanEnd});

        // Editable-multibuffer follow-up: resolve this excerpt's byte-exact
        // source range and register it as an ExcerptRange, so a later edit
        // in [bodyStart, spanEnd) has somewhere real to commit back to.
        // sourceStartLine == 0 ("no source line applies," e.g. a pure-
        // deletion diff hunk) or a failed line lookup (source vanished/
        // shrank since the caller counted lines) both silently fall back to
        // non-editable -- the same degrade-don't-crash posture
        // ReadExcerptText already takes toward a missing/changed source.
        if (excerpt.editable && excerpt.sourceStartLine > 0) {
            const std::string fullText = ReadFullSourceText(bufferList, excerpt.sourcePath);
            if (const auto range = LineRangeToByteRange(fullText, excerpt.sourceStartLine, excerpt.sourceEndLine)) {
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

    if (!excerpts.empty()) {
        // A closing rule so the last excerpt gets a visible bottom edge
        // too, matching every other excerpt's own top-rule framing.
        composite += MakeRuleLine();
        composite += '\n';
        lineTints.emplace_back(compositeLine, LineTint::Rule);
        ++compositeLine;
    }

    text::Buffer& results = bufferList.CreateBuffer(name);
    results.InsertAtPoint(composite);
    results.SetPoint(0);
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

CommitResult CommitExcerptChanges(text::BufferList& bufferList, text::Buffer& composite) {
    CommitResult result;

    // Grouped by source path -- ranges belonging to a different source file
    // don't interact, and within one file's own group they're applied in
    // descending source-byte order below (ApplyWorkspaceTextEdits's own
    // precedent in BufferView.cpp: keeps a not-yet-applied edit's stored
    // offset valid as an earlier, lower-offset edit shifts nothing above
    // it). Only ranges whose current composite text actually differs from
    // their originalText snapshot are collected -- an untouched excerpt is
    // left alone, not rewritten with identical content.
    std::map<std::filesystem::path, std::vector<std::size_t>> changedByPath;
    const std::vector<text::Buffer::ExcerptRange>&             ranges = composite.ExcerptRanges();
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const text::Buffer::ExcerptRange& range = ranges[i];
        if (!range.editable) {
            continue;
        }
        const std::string currentText = composite.Content().Substring(range.start, range.end - range.start);
        if (currentText != range.originalText) {
            changedByPath[range.sourcePath].push_back(i);
        }
    }

    for (auto& [path, indices] : changedByPath) {
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
        bool               conflicted  = false;
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
    }

    return result;
}

} // namespace ned::editor::multibuffer
