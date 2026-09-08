//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// The gutter/derived-data caches (every Ensure*Cache), the gutter column-layout
// queries, and the viewport geometry: scrolling, wrapping, and visible-line math.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

std::pair<std::size_t, std::size_t> BufferView::HugeStructuralWindow(const text::ITextStorage& content) const {
    const std::size_t byteLength = content.ByteLength();
    if (!content.IsHuge()) {
        return {0, byteLength};
    }

    const std::size_t margin    = editor::HugeStructuralWindowBytes();
    const std::size_t lineCount = content.LineCount();
    const std::size_t lastLine  = lineCount > 0 ? lineCount - 1 : 0;
    const std::size_t topLine   = std::min(topLine_, lastLine);
    const std::size_t viewportHeight = size().height > 0 ? static_cast<std::size_t>(size().height) : 1;
    const std::size_t bottomLine     = std::min(topLine + viewportHeight, lastLine);

    const std::size_t viewportStartByte = content.LineToByteOffset(topLine);
    const std::size_t viewportEndByte   = std::min(content.LineToByteOffset(bottomLine) + 1, byteLength);

    // Snapped to the start of whatever line it lands in, not a raw byte
    // subtraction -- confirmed empirically (not assumed) that feeding
    // tree-sitter a substring beginning mid-line/mid-token can desync its
    // parse badly enough to misidentify a spurious definition right at the
    // cut, or silently miss a real one shortly after it, well beyond the
    // "truncated at the tail" class of imprecision the trailing edge alone
    // has to worry about. A clean line start costs at most one extra line
    // of margin and gives the parser real, syntactically valid context from
    // its very first byte.
    const std::size_t rawWindowStart = viewportStartByte > margin ? viewportStartByte - margin : 0;
    const std::size_t windowStart    = content.LineToByteOffset(content.ByteOffsetToLine(rawWindowStart));
    const std::size_t windowEnd      = byteLength - viewportEndByte > margin ? viewportEndByte + margin : byteLength;
    return {windowStart, windowEnd};
}

void BufferView::EnsureFoldableBlocksCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (!FoldGutterActive()) {
        foldableBlocksCache_.clear();
        foldableBlocksCacheBuffer_     = &buffer;
        foldableBlocksCacheGeneration_ = buffer.ContentGeneration();
        return;
    }

    const text::ITextStorage&            content                = buffer.Content();
    const bool                           huge                   = content.IsHuge();
    const auto [windowStart, windowEnd]                         = HugeStructuralWindow(content);

    if (foldableBlocksCacheBuffer_ == &buffer && foldableBlocksCacheGeneration_ == buffer.ContentGeneration() &&
        foldableBlocksCacheWindowStart_ == windowStart && foldableBlocksCacheWindowEnd_ == windowEnd) {
        return;
    }

    // per-buffer-highlight-cache follow-up: persists across a buffer
    // switch -- see foldableBlocksCacheByBuffer_'s own doc comment in
    // BufferView.h, and highlightCacheByBuffer_'s for the full reasoning
    // this mirrors.
    const auto it = foldableBlocksCacheByBuffer_.find(&buffer);
    if (it == foldableBlocksCacheByBuffer_.end() || it->second.contentGeneration != buffer.ContentGeneration() ||
        it->second.modeName != mode_.name || it->second.windowStart != windowStart || it->second.windowEnd != windowEnd) {
        FoldableBlocksCacheEntry entry;
        // huge-file-structural-gutters follow-up: a huge buffer feeds
        // mode_.fold a bounded window (content.Substring) instead of the
        // whole document.
        entry.ranges = huge ? editor::codefold::FoldableBlocks(mode_, content.Substring(windowStart, windowEnd - windowStart))
                             : editor::codefold::FoldableBlocks(mode_, buffer.Text());
        if (huge) {
            // A block whose real closing brace lies beyond the window isn't
            // simply "not found" -- tree-sitter still emits a
            // compound_statement node via error recovery for the unclosed
            // "{", extending all the way to wherever the fed substring runs
            // out, and c-folds.scm's plain "(compound_statement) @fold"
            // captures it regardless (confirmed empirically, not assumed:
            // this is exactly what a first version of this fix's own test
            // caught). Reporting that truncated range as the block's real
            // extent would fold to an arbitrary window-edge line, not the
            // block's actual close -- worse than not finding it at all. Drop
            // any range whose end reaches the substring's own edge, the same
            // "don't trust a result abutting the window's own tail unless
            // the window reached the real document end" rule
            // Editor/HugeRegexScan.h already established for search.
            const std::size_t windowLength = windowEnd - windowStart;
            const bool        reachedDocumentEnd = windowEnd >= content.ByteLength();
            std::erase_if(entry.ranges, [&](const auto& range) { return !reachedDocumentEnd && range.second >= windowLength; });
            for (auto& [start, end] : entry.ranges) {
                start += windowStart;
                end += windowStart;
            }
        }
        entry.contentGeneration = buffer.ContentGeneration();
        entry.modeName          = mode_.name;
        entry.windowStart       = windowStart;
        entry.windowEnd         = windowEnd;
        foldableBlocksCache_    = entry.ranges;
        foldableBlocksCacheByBuffer_.insert_or_assign(&buffer, std::move(entry));
    }
    else {
        foldableBlocksCache_ = it->second.ranges;
    }
    foldableBlocksCacheBuffer_      = &buffer;
    foldableBlocksCacheGeneration_  = buffer.ContentGeneration();
    foldableBlocksCacheWindowStart_ = windowStart;
    foldableBlocksCacheWindowEnd_   = windowEnd;
}

void BufferView::EnsureEmbeddedDocumentCache() {
    text::Buffer& buffer = activeBuffer_.Get();

    if (!mode_.embeddedRegions) {
        embeddedDocumentCacheByBuffer_.erase(&buffer); // nothing to cache -- mode has no embedded regions at all
        return;
    }

    const auto it = embeddedDocumentCacheByBuffer_.find(&buffer);
    if (it != embeddedDocumentCacheByBuffer_.end() && it->second.contentGeneration == buffer.ContentGeneration() &&
        it->second.modeName == mode_.name) {
        return; // already current
    }

    EmbeddedDocumentCacheEntry entry;
    entry.documents         = editor::BuildEmbeddedDocuments(mode_, buffer.Text());
    entry.contentGeneration = buffer.ContentGeneration();
    entry.modeName          = mode_.name;
    embeddedDocumentCacheByBuffer_.insert_or_assign(&buffer, std::move(entry));
}

void BufferView::EnsureFoldGutterCache() const {
    EnsureFoldableBlocksCache();
    text::Buffer& buffer = activeBuffer_.Get();

    if (foldGutterCacheBuffer_ == &buffer && foldGutterCacheContentGeneration_ == buffer.ContentGeneration() &&
        foldGutterCacheFoldGeneration_ == buffer.FoldGeneration() &&
        foldGutterCacheWindowStart_ == foldableBlocksCacheWindowStart_ && foldGutterCacheWindowEnd_ == foldableBlocksCacheWindowEnd_) {
        return;
    }

    foldGutterEntries_.clear();
    for (auto& column : foldGutterLineRangesByColumn_) {
        column.clear();
    }

    if (!foldableBlocksCache_.empty()) {
        const text::ITextStorage& content = buffer.Content();
        const auto        regions = editor::codefold::FoldRegionsWithDepth(foldableBlocksCache_);

        foldGutterEntries_.reserve(regions.size());
        for (const auto& region : regions) {
            if (region.depth >= kMaxFoldDepthColumns) {
                // Deeper than the gutter has columns for: draw nothing at
                // all, rather than the previous clamp-into-the-last-column
                // behavior, which piled every deeper block's ⊞/⊟ and guide
                // line on top of the real depth-3 block's own -- visual
                // noise, and an ambiguous click target (a column-3 click
                // could land on whichever block happened to stack there).
                // The block itself stays fully foldable via
                // code-fold-toggle (M-x/keyboard path reads
                // FoldableBlocks, not these entries), and a deep block
                // collapsed that way still hides its lines and shows the
                // content-side ellipsis -- only the gutter affordance is
                // depth-capped.
                continue;
            }
            const int         column     = region.depth;
            const std::size_t headerLine = content.ByteOffsetToLine(region.startByte);
            const std::size_t closerLine = content.ByteOffsetToLine(region.endByte);
            if (headerLine == closerLine) {
                // A block written entirely on one line (e.g. a one-line
                // function body) has nothing to fold -- collapsing it would
                // hide zero lines (FoldedLineRanges' own [headerLine + 1,
                // closerLine + 1) is empty whenever they're equal), so the
                // gutter shows no ⊞/⊟ for it at all rather than a
                // clickable affordance that visibly does nothing. Purely a
                // rendering/click-target filter -- FoldRegionsWithDepth
                // above still computed this region's real depth, so a
                // *nested* multi-line block still gets its own correct
                // column regardless of a one-line sibling/ancestor skipped
                // here.
                continue;
            }
            if (!foldGutterEntries_.empty() && foldGutterEntries_.back().headerLine == headerLine) {
                // Only the outermost block opening on a line gets a gutter
                // affordance (lisp-nesting fix, clojure-and-jank follow-up:
                // `:profiles {:dev {:dependencies [...` used to stack three
                // ⊞/⊟ columns on one row). Two multi-line blocks sharing a
                // header line are necessarily nested -- a sibling can't
                // start before the previous multi-line block's closing line
                // -- and regions arrive sorted by startByte, so the first
                // entry added for a line is always the outermost; everything
                // after it here is an inner block whose fold the outer one's
                // covers. ToggleFoldAtLine's outermost-wins rule (CodeFold.h)
                // is the keyboard half of this same decision.
                continue;
            }
            foldGutterEntries_.push_back(FoldGutterEntry{
                .headerLine = headerLine,
                .closerLine = closerLine,
                .blockStart = region.startByte,
                .column     = column,
            });
            if (!buffer.FoldMarkerAt(region.startByte).has_value()) {
                // Expanded -- gets a guide line down to (and including) its
                // closer. A collapsed block gets no line at all, only its own
                // ⊞ on its header row -- there's nothing to trace while its
                // body is hidden (an explicit user choice, not an oversight).
                foldGutterLineRangesByColumn_[column].emplace_back(headerLine + 1, closerLine + 1);
            }
        }
    }

    foldGutterCacheBuffer_            = &buffer;
    foldGutterCacheContentGeneration_ = buffer.ContentGeneration();
    foldGutterCacheFoldGeneration_    = buffer.FoldGeneration();
    foldGutterCacheWindowStart_       = foldableBlocksCacheWindowStart_;
    foldGutterCacheWindowEnd_         = foldableBlocksCacheWindowEnd_;
}

void BufferView::EnsureUnsavedChangeCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (unsavedChangeCacheBuffer_ == &buffer && unsavedChangeCacheContentGeneration_ == buffer.ContentGeneration() &&
        unsavedChangeCacheGeneration_ == buffer.UnsavedChangeGeneration()) {
        return;
    }

    unsavedChangeLineRanges_.clear();
    const text::ITextStorage& content = buffer.Content();
    for (const auto& [byteStart, byteEnd] : buffer.UnsavedChangeRanges()) {
        const std::size_t startLine = content.ByteOffsetToLine(byteStart);
        // byteEnd is exclusive and may sit exactly on a line boundary (the
        // byte after the range's own last one) -- back it up by one before
        // converting so a range that ends right at "line N+1, column 0"
        // doesn't get counted as touching line N+1 too.
        const std::size_t endLine = content.ByteOffsetToLine(byteEnd > byteStart ? byteEnd - 1 : byteStart);
        // UnsavedChangeRanges() arrives sorted by byte offset, so startLine
        // here is never less than the previous entry's -- merge with the
        // last pushed range if they touch or overlap, same "already
        // sorted, just coalesce adjacent" approach used elsewhere in this
        // codebase (e.g. Buffer's own MergeUnsavedRange).
        if (!unsavedChangeLineRanges_.empty() && startLine <= unsavedChangeLineRanges_.back().second) {
            unsavedChangeLineRanges_.back().second = std::max(unsavedChangeLineRanges_.back().second, endLine + 1);
        }
        else {
            unsavedChangeLineRanges_.emplace_back(startLine, endLine + 1);
        }
    }

    unsavedChangeCacheBuffer_            = &buffer;
    unsavedChangeCacheContentGeneration_ = buffer.ContentGeneration();
    unsavedChangeCacheGeneration_        = buffer.UnsavedChangeGeneration();
}

void BufferView::EnsureDiagnosticGutterCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (diagnosticGutterCacheBuffer_ == &buffer && diagnosticGutterCacheGeneration_ == buffer.DiagnosticsGeneration()) {
        return;
    }

    diagnosticLineSeverities_.clear();
    const text::ITextStorage& content = buffer.Content();
    // Diagnostics() arrives in whatever order the server reported them, not
    // necessarily sorted by position -- collapse to at most one {line,
    // severity} entry per line (keeping the most severe) via a small local
    // map, then sort by line once at the end for the per-row lower_bound
    // lookup Paint() does.
    std::unordered_map<std::size_t, text::Buffer::Diagnostic::Severity> mostSevereByLine;
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        const std::size_t line = content.ByteOffsetToLine(diagnostic.startByte);
        const auto        it   = mostSevereByLine.find(line);
        if (it == mostSevereByLine.end() || DiagnosticSeverityRank(diagnostic.severity) > DiagnosticSeverityRank(it->second)) {
            mostSevereByLine[line] = diagnostic.severity;
        }
    }
    diagnosticLineSeverities_.assign(mostSevereByLine.begin(), mostSevereByLine.end());
    std::sort(diagnosticLineSeverities_.begin(), diagnosticLineSeverities_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    diagnosticGutterCacheBuffer_     = &buffer;
    diagnosticGutterCacheGeneration_ = buffer.DiagnosticsGeneration();
}

void BufferView::EnsureSymbolMarkersCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    // Eligibility gate -- mirrors FoldGutterActive's own mode_.fold/
    // ReadOnly() reasoning (a real query run against a synthesized
    // "path:line: text" results buffer produces meaningless markers, not an
    // empty result). Stamped as up to date even when ineligible so a repeat
    // call this same frame/buffer stays a cheap no-op.
    if (!mode_.symbolKind || buffer.ReadOnly()) {
        symbolMarkersCache_.clear();
        symbolMarkersCacheBuffer_            = &buffer;
        symbolMarkersCacheContentGeneration_ = buffer.ContentGeneration();
        return;
    }

    const text::ITextStorage& content  = buffer.Content();
    const bool                huge     = content.IsHuge();
    const auto [windowStart, windowEnd] = HugeStructuralWindow(content);

    if (symbolMarkersCacheBuffer_ == &buffer && symbolMarkersCacheContentGeneration_ == buffer.ContentGeneration() &&
        symbolMarkersCacheWindowStart_ == windowStart && symbolMarkersCacheWindowEnd_ == windowEnd) {
        return;
    }

    // huge-file-structural-gutters follow-up: a huge buffer feeds
    // mode_.symbolKind a bounded window instead of the whole document --
    // both startByte and endByte are then window-relative, remapped back to
    // absolute buffer coordinates (+= windowStart) here so every consumer
    // (the gutter below, sticky scroll) can treat this cache's coordinates
    // uniformly regardless of buffer size.
    symbolMarkersCache_ =
        huge ? mode_.symbolKind(content.Substring(windowStart, windowEnd - windowStart)) : mode_.symbolKind(buffer.Text());
    if (huge) {
        for (editor::SymbolMarker& marker : symbolMarkersCache_) {
            marker.startByte += windowStart;
            marker.endByte += windowStart;
        }
    }
    symbolMarkersCacheBuffer_            = &buffer;
    symbolMarkersCacheContentGeneration_ = buffer.ContentGeneration();
    symbolMarkersCacheWindowStart_       = windowStart;
    symbolMarkersCacheWindowEnd_         = windowEnd;
}

void BufferView::EnsureSymbolGutterCache() const {
    EnsureSymbolMarkersCache();
    text::Buffer& buffer = activeBuffer_.Get();

    if (symbolGutterCacheBuffer_ == &buffer && symbolGutterCacheContentGeneration_ == buffer.ContentGeneration() &&
        symbolGutterCacheWindowStart_ == symbolMarkersCacheWindowStart_ &&
        symbolGutterCacheWindowEnd_ == symbolMarkersCacheWindowEnd_) {
        return;
    }

    // symbolMarkersCache_ arrives sorted by startByte (Mode.cpp's own
    // closure) -- collapsing to one entry per line via a plain overwrite in
    // that order keeps the LAST (highest-byte-offset) marker on a line that
    // somehow has more than one, the same "later wins" convention
    // HighlightSpan's own doc comment establishes elsewhere in this file.
    const text::ITextStorage&                           content = buffer.Content();
    std::unordered_map<std::size_t, editor::SymbolKind> kindByLine;
    for (const editor::SymbolMarker& marker : symbolMarkersCache_) {
        kindByLine[content.ByteOffsetToLine(marker.startByte)] = marker.kind;
    }
    symbolGutterLineKinds_.assign(kindByLine.begin(), kindByLine.end());
    std::sort(symbolGutterLineKinds_.begin(), symbolGutterLineKinds_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    symbolGutterCacheBuffer_            = &buffer;
    symbolGutterCacheContentGeneration_ = buffer.ContentGeneration();
    symbolGutterCacheWindowStart_       = symbolMarkersCacheWindowStart_;
    symbolGutterCacheWindowEnd_         = symbolMarkersCacheWindowEnd_;

    symbolGutterCacheBuffer_            = &buffer;
    symbolGutterCacheContentGeneration_ = buffer.ContentGeneration();
    symbolGutterCacheWindowStart_       = symbolMarkersCacheWindowStart_;
    symbolGutterCacheWindowEnd_         = symbolMarkersCacheWindowEnd_;
}

void BufferView::EnsureConflictHunkCache() const {
    text::Buffer& buffer = activeBuffer_.Get();
    if (conflictHunkCacheBuffer_ == &buffer && conflictHunkCacheContentGeneration_ == buffer.ContentGeneration()) {
        return;
    }
    conflictHunkCache_                  = text::ParseConflictHunks(buffer.Text());
    conflictHunkCacheBuffer_            = &buffer;
    conflictHunkCacheContentGeneration_ = buffer.ContentGeneration();
}

void BufferView::EnsureTestGutterCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    // Eligibility gate, EnsureSymbolGutterCache's exact shape -- plus the
    // runner itself: no runner wired (tests) means nothing to mark, at zero
    // width.
    //
    // test-runner-gaps follow-up: a parsed outcome is no longer required.
    // With none yet, a discovered test still gets a row -- the clickable
    // "run this test" affordance -- but only when a filter command is
    // actually configured, since that is exactly what makes the click able
    // to do anything (run-test-at-point refuses without one). A user who
    // never configured ned/set-test-filter-command therefore sees this
    // gutter behave exactly as it did before: nothing until a run happens.
    //
    // Split in two so the free checks short-circuit before the config
    // lookup: this runs once per pane per frame, and HasTestFilterCommand
    // still takes a mutex (TestFilterCommand() would additionally copy the
    // argv vector out, which is why it isn't used here).
    if (!mode_.testDiscovery || buffer.ReadOnly() || testRunner_ == nullptr) {
        testGutterEntries_.clear();
        testGutterCacheBuffer_            = &buffer;
        testGutterCacheContentGeneration_ = buffer.ContentGeneration();
        testGutterCacheOutcomeGeneration_ = testRunner_ != nullptr ? testRunner_->OutcomeGeneration() : 0;
        testGutterCacheRunnable_          = false; // no runner -- no affordance is possible either
        return;
    }
    const bool runnableAffordance = editor::testrun::HasTestFilterCommand();
    if (!testRunner_->LatestOutcome() && !runnableAffordance) {
        testGutterEntries_.clear();
        testGutterCacheBuffer_            = &buffer;
        testGutterCacheContentGeneration_ = buffer.ContentGeneration();
        testGutterCacheOutcomeGeneration_ = testRunner_->OutcomeGeneration();
        testGutterCacheRunnable_          = runnableAffordance;
        return;
    }

    const text::ITextStorage& content              = buffer.Content();
    const bool                huge                 = content.IsHuge();
    const auto [windowStart, windowEnd]             = HugeStructuralWindow(content);

    // testGutterCacheRunnable_ is part of the key, not just an input: a
    // filter command configured after a run has already landed changes what
    // rows exist without touching content, outcome, or window generation.
    if (testGutterCacheBuffer_ == &buffer && testGutterCacheContentGeneration_ == buffer.ContentGeneration() &&
        testGutterCacheOutcomeGeneration_ == testRunner_->OutcomeGeneration() && testGutterCacheWindowStart_ == windowStart &&
        testGutterCacheWindowEnd_ == windowEnd && testGutterCacheRunnable_ == runnableAffordance) {
        return;
    }

    // May hold nothing at all now (the runnable-affordance case above): the
    // marker loop then finds no matching result for any test and every row
    // comes out status-less, which is exactly the pre-run state.
    static const editor::testrun::TestRunOutcome kNoOutcome{};
    const editor::testrun::TestRunOutcome&       outcome =
        testRunner_->LatestOutcome() ? *testRunner_->LatestOutcome() : kNoOutcome;
    const std::string bufferBasename = buffer.Path() ? buffer.Path()->filename().string() : std::string();

    testGutterEntries_.clear();
    // huge-file-structural-gutters follow-up: a huge buffer feeds
    // mode_.testDiscovery a bounded window instead of the whole document --
    // marker.startByte is then window-relative, remapped back to absolute
    // buffer coordinates (+= windowStart) before the ByteOffsetToLine call
    // below.
    for (const editor::TestMarker& marker :
         huge ? mode_.testDiscovery(content.Substring(windowStart, windowEnd - windowStart)) : mode_.testDiscovery(buffer.Text())) {
        const std::size_t startByte = huge ? marker.startByte + windowStart : marker.startByte;
        // Aggregate every matching result (parameterized instances, go
        // subtests): Failed beats Passed beats Skipped. The result's file,
        // when it names one at all, is only a basename-level *filter*
        // against cross-file name collisions -- the name is the real key
        // (results carry cwd-relative or basename-only paths, see
        // TestOutputParser.h; anything path-shaped stricter than a
        // basename comparison would reject its own legitimate matches).
        std::optional<editor::testrun::TestResult::Status> aggregate;
        for (const editor::testrun::TestResult& result : outcome.results) {
            if (!editor::testrun::MatchesTestName(marker.name, result.name)) {
                continue;
            }
            if (!result.file.empty() && !bufferBasename.empty() &&
                std::filesystem::path(result.file).filename().string() != bufferBasename) {
                continue;
            }
            if (result.status == editor::testrun::TestResult::Status::Failed) {
                aggregate = result.status;
                break;
            }
            if (!aggregate || (aggregate == editor::testrun::TestResult::Status::Skipped &&
                               result.status == editor::testrun::TestResult::Status::Passed)) {
                aggregate = result.status;
            }
        }
        // A discovered test with no result at all reads as "passed" only
        // when the format never names passing tests, the run was a full
        // (unfiltered) one, and the output genuinely parsed -- otherwise
        // absence means "not run", which gets no mark rather than a guess.
        if (!aggregate && outcome.failuresOnly && outcome.parsedOk && !testRunner_->LastRunWasFiltered()) {
            aggregate = editor::testrun::TestResult::Status::Passed;
        }
        // test-runner-gaps follow-up: a status-less row is kept (rather than
        // skipped as it used to be) only when the runnable affordance is on
        // -- that row paints '▸' and is what a gutter click runs. Without a
        // filter command configured, absence still means no row at all.
        if (aggregate || runnableAffordance) {
            testGutterEntries_.push_back(TestGutterEntry{
                .line   = content.ByteOffsetToLine(startByte),
                .status = aggregate,
                .name   = marker.name,
            });
        }
    }
    // One entry per line, Failed winning a same-line tie (a class marker and
    // a same-line method can't collide in practice, but two markers on one
    // line must not produce two sort keys). A status-less (not-run) row
    // ranks last, so a line carrying both a real result and a bare runnable
    // marker keeps the result.
    const auto tieRank = [](const std::optional<editor::testrun::TestResult::Status>& status) {
        if (!status) {
            return 3;
        }
        switch (*status) {
            case editor::testrun::TestResult::Status::Failed:
                return 0;
            case editor::testrun::TestResult::Status::Passed:
                return 1;
            case editor::testrun::TestResult::Status::Skipped:
                return 2;
        }
        return 4;
    };
    std::sort(testGutterEntries_.begin(), testGutterEntries_.end(), [&](const TestGutterEntry& a, const TestGutterEntry& b) {
        return a.line != b.line ? a.line < b.line : tieRank(a.status) < tieRank(b.status);
    });
    testGutterEntries_.erase(
        std::unique(testGutterEntries_.begin(), testGutterEntries_.end(),
                    [](const TestGutterEntry& a, const TestGutterEntry& b) { return a.line == b.line; }),
        testGutterEntries_.end());

    testGutterCacheBuffer_            = &buffer;
    testGutterCacheContentGeneration_ = buffer.ContentGeneration();
    testGutterCacheOutcomeGeneration_ = testRunner_->OutcomeGeneration();
    testGutterCacheWindowStart_       = windowStart;
    testGutterCacheWindowEnd_         = windowEnd;
    testGutterCacheRunnable_          = runnableAffordance;
}

void BufferView::EnsureCoverageGutterCache() const {
    text::Buffer&     buffer           = activeBuffer_.Get();
    const std::size_t reportGeneration = editor::coverage::CoverageReportGeneration();
    if (coverageGutterCacheBuffer_ == &buffer && coverageGutterCacheReportGeneration_ == reportGeneration) {
        return;
    }

    coverageGutterLineStatuses_.clear();
    coverageGutterCacheBuffer_           = &buffer;
    coverageGutterCacheReportGeneration_ = reportGeneration;

    if (!buffer.Path()) {
        return; // unsaved/scratch buffer -- nothing to match a coverage report's SF: path against
    }

    const editor::coverage::CoverageReport report = editor::coverage::CurrentCoverageReport();
    const editor::coverage::FileCoverage*  file =
        editor::coverage::FindFileCoverage(report, *buffer.Path(), editor::ProjectRoot());
    if (file == nullptr) {
        return;
    }

    coverageGutterLineStatuses_.reserve(file->lines.size());
    for (const editor::coverage::LineCoverage& line : file->lines) {
        coverageGutterLineStatuses_.emplace_back(line.line, line.Status());
    }
    // file->lines is already sorted-by-line/unique-per-line by construction
    // (CoverageOutputParser.h's own merge step keeps it that way), so no
    // sort/dedupe pass is needed here the way testGutterEntries_ above
    // needs one (multiple test markers can share a line; coverage lines
    // can't).
}

void BufferView::EnsureInlineDiagnosticCache() const {
    text::Buffer& buffer = activeBuffer_.Get();
    if (inlineDiagnosticCacheBuffer_ == &buffer && inlineDiagnosticCacheDiagGeneration_ == buffer.DiagnosticsGeneration() &&
        inlineDiagnosticCacheContentGeneration_ == buffer.ContentGeneration()) {
        return;
    }

    inlineDiagnosticsByLine_.clear();
    const text::ITextStorage& content = buffer.Content();
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        // prose-diagnostic-callout follow-up: the prose/grammar checker's
        // diagnostics never get this code-style caret+message annotation
        // row -- see PaintProseDiagnosticCallouts instead.
        if (diagnostic.origin != text::Buffer::Diagnostic::Origin::Code) {
            continue;
        }
        const std::size_t line = content.ByteOffsetToLine(std::min(diagnostic.startByte, content.ByteLength()));
        const auto        it   = inlineDiagnosticsByLine_.find(line);
        const bool        replaces =
            it == inlineDiagnosticsByLine_.end() ||
            DiagnosticSeverityRank(diagnostic.severity) > DiagnosticSeverityRank(it->second.severity) ||
            (DiagnosticSeverityRank(diagnostic.severity) == DiagnosticSeverityRank(it->second.severity) &&
             diagnostic.startByte < it->second.startByte);
        if (!replaces) {
            continue;
        }
        // First line of the message only -- one annotation row per line,
        // "within reason" (clangd's notes/fix-its can make these multiline).
        std::string message            = diagnostic.message.substr(0, diagnostic.message.find('\n'));
        inlineDiagnosticsByLine_[line] = InlineDiagnostic{
            .severity  = diagnostic.severity,
            .startByte = diagnostic.startByte,
            .endByte   = std::max(diagnostic.endByte, diagnostic.startByte + 1), // widen zero-length spans, same as the underline pass
            .message   = std::move(message),
        };
    }

    inlineDiagnosticCacheBuffer_            = &buffer;
    inlineDiagnosticCacheDiagGeneration_    = buffer.DiagnosticsGeneration();
    inlineDiagnosticCacheContentGeneration_ = buffer.ContentGeneration();
}

void BufferView::EnsureBlameGutterCache() const {
    text::Buffer& buffer = activeBuffer_.Get();
    if (blameGutterCacheBuffer_ == &buffer && blameGutterCacheContentGeneration_ == buffer.ContentGeneration()) {
        return; // still valid for this buffer/content -- nothing to do (see this method's own header comment)
    }
    // Either the active buffer changed, or its content did since blame was
    // last populated -- either way, blameLineInfo_ no longer corresponds to
    // real line attribution. Clear it rather than trying to resynthesize
    // it; a fresh vcs-show-blame call is what repopulates it.
    blameLineInfo_.clear();
    blameGutterCacheBuffer_            = &buffer;
    blameGutterCacheContentGeneration_ = buffer.ContentGeneration();
}

bool BufferView::BlameGutterActive() const {
    return !blameLineInfo_.empty();
}

void BufferView::EnsureHiddenLineRangesCache() const {
    text::Buffer& buffer = activeBuffer_.Get();

    if (buffer.FoldMarkers().empty()) {
        // Fast path: every buffer that's never had org-cycle/code-fold-toggle
        // run at all (i.e. every non-Org, non-folded buffer) takes this
        // branch -- no call into org::FoldedLineRanges/codefold::FoldedLineRanges,
        // no outline re-parse or fold-query call, at all.
        hiddenLineRanges_.clear();
        hiddenLineRangesCacheBuffer_            = &buffer;
        hiddenLineRangesCacheContentGeneration_ = buffer.ContentGeneration();
        hiddenLineRangesCacheFoldGeneration_    = buffer.FoldGeneration();
        return;
    }

    if (hiddenLineRangesCacheBuffer_ == &buffer &&
        hiddenLineRangesCacheContentGeneration_ == buffer.ContentGeneration() &&
        hiddenLineRangesCacheFoldGeneration_ == buffer.FoldGeneration()) {
        return;
    }

    // Auto-collapse-on-build follow-up: a multibuffer (*vcs diff*/*vcs
    // commit*/*references*/*diagnostics*/...) is checked first -- its
    // mode_ is always a plain FundamentalMode (no fold query, path-less
    // buffer), so it would otherwise fall through to the org branch below
    // and never show any of BuildMultibuffer's own auto-collapsed excerpts.
    // FoldableExcerptBlocks derives codefold::FoldedLineRanges' own "blocks"
    // shape from the excerpt spans themselves -- no tree-sitter/mode
    // involved, same function either way.
    if (editor::multibuffer::MultibufferIndex* index = editor::multibuffer::MultibufferIndexFor(buffer)) {
        hiddenLineRanges_ =
            editor::codefold::FoldedLineRanges(buffer, buffer.Content(), editor::multibuffer::FoldableExcerptBlocks(*index));
    }
    // generic-code-folding follow-up: only a mode with a real fold query
    // goes through the new generic tree-sitter-block path -- every other
    // mode (org-mode included, which has none: it drives FoldMarkers_
    // entirely through org::CycleFoldAtPoint, mode-independently) keeps the
    // original org::FoldedLineRanges path exactly as before. This isn't
    // really "Org-specific" so much as "the only interpretation that
    // existed before this feature" -- preserving it for every mode::fold-
    // less buffer is what keeps pre-existing direct-FoldMarkers_ usage
    // (e.g. a plain FundamentalMode buffer with a marker set by hand)
    // working exactly as it always has.
    else if (mode_.fold) {
        EnsureFoldableBlocksCache();
        hiddenLineRanges_ = editor::codefold::FoldedLineRanges(buffer, buffer.Content(), foldableBlocksCache_);
    }
    else {
        hiddenLineRanges_ = editor::org::FoldedLineRanges(buffer);
    }
    hiddenLineRangesCacheBuffer_            = &buffer;
    hiddenLineRangesCacheContentGeneration_ = buffer.ContentGeneration();
    hiddenLineRangesCacheFoldGeneration_    = buffer.FoldGeneration();
}

void BufferView::EnsureLinkCache() const {
    if (mode_.name != "org-mode") {
        // Fast path: every non-Org buffer never even reaches org::ParseLinks
        // -- see this cache's own doc comment in BufferView.h.
        linkCache_.clear();
        linkCacheBuffer_     = nullptr;
        linkCacheGeneration_ = 0;
        return;
    }

    text::Buffer& buffer = activeBuffer_.Get();
    if (linkCacheBuffer_ == &buffer && linkCacheGeneration_ == buffer.ContentGeneration()) {
        return;
    }

    linkCache_           = editor::org::ParseLinks(buffer.Text());
    linkCacheBuffer_     = &buffer;
    linkCacheGeneration_ = buffer.ContentGeneration();
}

bool BufferView::IsLineHidden(std::size_t line) const {
    EnsureHiddenLineRangesCache();
    for (const auto& [start, end] : hiddenLineRanges_) {
        if (line >= start && line < end)
            return true;
    }
    return false;
}

std::size_t BufferView::NextVisibleLine(std::size_t line, std::size_t limit) const {
    while (line < limit && IsLineHidden(line))
        ++line;
    return line;
}

std::size_t BufferView::AdvanceVisibleLines(std::size_t line, std::size_t count, std::size_t limit) const {
    while (count > 0 && line < limit) {
        line = NextVisibleLine(line + 1, limit);
        --count;
    }
    return line;
}

std::size_t BufferView::VisibleLineCountBetween(std::size_t startLine, std::size_t endLineExclusive) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        if (!IsLineHidden(line))
            ++count;
    }
    return count;
}

void BufferView::EnsureRowCountCache() const {
    text::Buffer&     buffer       = activeBuffer_.Get();
    const bool        wrapEnabled  = EffectiveWrapLines();
    const std::size_t gutterWidth  = GutterWidth();
    const int         contentWidth = std::max(1, size().width - static_cast<int>(gutterWidth));

    if (!wrapEnabled) {
        // Fast path: every buffer with wrap off (the common case) never
        // needs a real per-line row count at all -- RowsForLine's own "1
        // when !wrapEnabled" branch below never even looks at
        // rowCountPerLine_ in that case, so this just keeps the cache keys
        // themselves current without ever calling ComputeWrapSegments.
        rowCountPerLine_.clear();
        rowCountCacheBuffer_            = &buffer;
        rowCountCacheContentGeneration_ = buffer.ContentGeneration();
        rowCountCacheContentWidth_      = contentWidth;
        rowCountCacheWrapEnabled_       = false;
        return;
    }

    if (rowCountCacheBuffer_ == &buffer && rowCountCacheContentGeneration_ == buffer.ContentGeneration() &&
        rowCountCacheContentWidth_ == contentWidth && rowCountCacheWrapEnabled_ == wrapEnabled) {
        return; // still valid -- whatever's already memoized in rowCountPerLine_ (per RowsForLine) stays
    }

    // line-wrap follow-up: only resets the cache's sizing/keys here (a
    // cheap sentinel fill, not real work) -- RowsForLine below is what
    // actually computes and memoizes one line's row count, lazily, the
    // first time that specific line is asked about. An earlier version
    // eagerly computed every line's real word-break scan right here, which
    // a [Performance] test caught as a genuine regression: MaxTopLine()/
    // ScrollToShowPoint() run every Paint() call, so an eager whole-buffer
    // scan here made every single Paint() call on a huge wrap-enabled
    // document pay for the full document's word-break cost up front.
    rowCountPerLine_.assign(buffer.Content().LineCount(), kRowCountUnknown);
    rowCountCacheBuffer_            = &buffer;
    rowCountCacheContentGeneration_ = buffer.ContentGeneration();
    rowCountCacheContentWidth_      = contentWidth;
    rowCountCacheWrapEnabled_       = wrapEnabled;
}

std::size_t BufferView::RowsForLine(std::size_t line) const {
    if (IsLineHidden(line)) {
        return 0; // hidden hides the annotation row too -- a fold swallows the whole line
    }
    // inline-diagnostics follow-up: an annotated line reports one extra row
    // here, at the single source every row-math consumer already shares
    // (CursorPosition, ScrollToShowPoint, MaxTopLine, ByteOffsetForPoint,
    // VisibleRowCountBetween/AtLeast) -- the same seam wrap continuation
    // rows ride, so none of them can disagree about where an annotation
    // shifted the rows below it.
    const std::size_t annotationRows = AnnotationRowsForLine(line);
    // codeLens follow-up: LeadingAnnotationRowsForLine's own 0-or-1 count,
    // added at the same three return points annotationRows already is --
    // see that method's own doc comment for why this is a leading
    // (above-the-line) row rather than another trailing one.
    const std::size_t leadingRows = LeadingAnnotationRowsForLine(line);
    if (!EffectiveWrapLines()) {
        return 1 + leadingRows + annotationRows;
    }
    EnsureRowCountCache();
    if (line >= rowCountPerLine_.size()) {
        return 1 + leadingRows + annotationRows;
    }
    if (rowCountPerLine_[line] == kRowCountUnknown) {
        // line-wrap follow-up: the real, lazy, per-line word-break scan --
        // computed and memoized only for a line actually asked about, never
        // eagerly for the whole buffer (see EnsureRowCountCache's own doc
        // comment for why that distinction is load-bearing, not cosmetic).
        text::Buffer&     buffer    = activeBuffer_.Get();
        const text::ITextStorage& content   = buffer.Content();
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        EnsureLinkCache();
        const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, buffer.Point());
        rowCountPerLine_[line] =
            ComputeWrappedLineSegments(content, lineStart, lineEnd, rowCountCacheContentWidth_, lineLinks).size();
    }
    return rowCountPerLine_[line] + leadingRows + annotationRows; // memoized value is content rows only -- annotation state changes independently of the wrap cache's keys
}

std::size_t BufferView::VisibleRowCountBetween(std::size_t startLine, std::size_t endLineExclusive) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        count += RowsForLine(line);
    }
    return count;
}

bool BufferView::VisibleRowCountAtLeast(std::size_t startLine, std::size_t endLineExclusive, std::size_t limit) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        count += RowsForLine(line);
        if (count >= limit) {
            return true;
        }
    }
    return false;
}

void BufferView::ClampPointToNarrowing() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.IsNarrowed()) {
        return;
    }
    const auto [start, end] = buffer.NarrowedRange();
    // end is exclusive (the excluded next line's own start byte, or
    // ByteLength() if there is none) -- allowing point to sit exactly at
    // end would let it rest at that excluded line's own start, which
    // Content().ByteOffsetToLine (and so the mode line's own L: indicator)
    // correctly, if confusingly, reports as *being on* the excluded line --
    // a real, confirmed-via-manual-pty-testing bug, not a hypothetical one.
    // The largest valid point is one byte before end: the end of the
    // narrowed range's own last line, right before its trailing newline,
    // matching Buffer::NarrowToRegion's own identical clamp.
    const std::size_t maxPoint = end > start ? end - 1 : start;
    const std::size_t point    = buffer.Point();
    if (point < start || point > maxPoint) {
        buffer.SetPoint(std::clamp(point, start, maxPoint));
    }
}

std::size_t BufferView::ByteOffsetForPoint(Point at) const {
    // Org-mode fold/unfold follow-up: was topLine_ + at.y, a flat 1:1
    // mapping -- a click on screen row N means the N-th *visible* buffer
    // line below topLine_, not literally topLine_ + N, whenever a fold is
    // hiding lines above the click. line-wrap follow-up: was
    // AdvanceVisibleLines (pure line-stepping, 1 row per visible line);
    // now a row-aware walk that consumes RowsForLine(line) rows per line
    // instead, additionally reporting which segment of the landed-on line
    // the target row corresponds to.
    const text::Buffer& buffer     = activeBuffer_.Get();
    const text::ITextStorage&   content    = buffer.Content();
    const std::size_t   totalLines = content.LineCount();

    // main-editor-sticky-scroll follow-up: at.y is in this pane's own local
    // coordinates, unaware that Paint() may have pushed real content down by
    // stickyRowCount_ rows this frame -- subtracted here so a click below
    // the pinned rows still resolves to the buffer line actually drawn
    // there. A click landing IN the pinned band itself (result would be
    // negative) clamps to row 0, same as the existing at.y<0 defensive
    // clamp below.
    std::size_t targetRow     = static_cast<std::size_t>(std::max(at.y - stickyRowCount_, 0));
    std::size_t line          = topLine_;
    std::size_t segmentInLine = 0;
    while (line < totalLines) {
        const std::size_t rows = RowsForLine(line);
        if (rows == 0) {
            line = NextVisibleLine(line + 1, totalLines);
            continue;
        }
        if (targetRow < rows) {
            segmentInLine = targetRow;
            break;
        }
        targetRow -= rows;
        line = NextVisibleLine(line + 1, totalLines);
    }
    line = std::min(line, totalLines - 1); // mirrors Buffer::ByteOffsetForLineAndColumn's own clamp

    const std::size_t x           = static_cast<std::size_t>(std::max(at.x, 0));
    const std::size_t gutterWidth = GutterWidth();
    // A click inside the gutter itself lands on that line's first column,
    // same as clicking right at the start of the line's text. line-wrap
    // follow-up: leftColumn_ added back on -- the click's on-screen column
    // has to be translated back to the line's own column space, the same
    // "screen column = real column - leftColumn_" relationship Paint()'s own
    // fast-forward phase established for drawing. Always 0 once
    // EffectiveWrapLines() is true, a no-op then.
    const std::size_t column = (x > gutterWidth) ? x - gutterWidth + leftColumn_ : leftColumn_;

    // Links follow-up: was a direct Buffer::ByteOffsetForLineAndColumn call
    // -- that method must stay entirely link-oblivious (Buffer has zero
    // Org-specific knowledge), so this reimplements its same tab-aware walk
    // locally, link-aware, via ByteOffsetForColumnInLine (see its own doc
    // comment above for why it's safe to call unconditionally here even in
    // a non-Org buffer). lineLinks is built against buffer.Point() -- the
    // buffer's point *before* this click resolves -- so it excludes exactly
    // the links Paint() left uncollapsed the last time this row was
    // actually drawn.
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd   = (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    EnsureLinkCache();
    const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, buffer.Point());

    // line-wrap follow-up: resolve the click against the landed-on
    // segment's own [startByte, endByte) instead of the whole line's range
    // -- lineLinks is still the whole line's own set (matches Paint()'s own
    // "compute once per line, reuse per segment" shape), just the byte
    // range being searched narrows to this one row.
    std::size_t segStart = lineStart;
    std::size_t segEnd   = lineEnd;
    if (EffectiveWrapLines()) {
        const int                      fullWidth      = std::max(1, size().width - static_cast<int>(gutterWidth));
        const std::vector<WrapSegment> segments       = ComputeWrappedLineSegments(content, lineStart, lineEnd, fullWidth, lineLinks);
        const std::size_t              clampedSegment = std::min(segmentInLine, segments.size() - 1);
        segStart                                      = segments[clampedSegment].startByte;
        segEnd                                        = segments[clampedSegment].endByte;
    }

    return ByteOffsetForColumnInLine(content, segStart, segEnd, column, editor::TabWidth(), lineLinks);
}

bool BufferView::FoldGutterActive() const {
    return mode_.fold && editor::CodeFoldingEnabled() && !activeBuffer_.Get().ReadOnly();
}

std::size_t BufferView::GutterWidth() const {
    const std::size_t totalLines = activeBuffer_.Get().Content().LineCount();
    // status/line-number-spacing follow-up (LSP client follow-up: gained a
    // second, dedicated diagnostic column -- see kDiagnosticWidth's own doc
    // comment): [status][diagnostic][gap][digits][gap][symbol][fold], left
    // to right -- status and diagnostic are always reserved; the fold region
    // (generic-code-folding / depth-aware-fold-gutter follow-ups) only when
    // a mode has a real fold query and the feature is enabled, a fixed
    // kMaxFoldDepthColumns-wide reservation (not one that grows with how
    // deep the currently-visible content happens to nest -- an explicit
    // user choice, so the gutter's own width never jumps around while
    // scrolling past a deeply nested region). symbol (gutter-symbol-kind
    // follow-up), unlike fold, IS data-driven -- see SymbolGutterActive's
    // own doc comment for why.
    const std::size_t foldColumn     = FoldGutterActive() ? kMaxFoldDepthColumns : 0;
    const std::size_t blameColumn    = BlameGutterActive() ? kBlameWidth : 0;
    const std::size_t diffColumn     = DiffGutterActive() ? kDiffWidth : 0;
    const std::size_t dapColumn      = DapGutterActive() ? kDapWidth : 0;
    const std::size_t symbolColumn   = SymbolGutterActive() ? kSymbolWidth : 0;
    const std::size_t testColumn     = TestGutterActive() ? kTestWidth : 0;
    const std::size_t coverageColumn = CoverageGutterActive() ? kCoverageWidth : 0;
    // Multibuffers follow-up: the line-number digits + both surrounding
    // gaps collapse to zero width together when LineNumberGutterActive()
    // is false -- see its own doc comment.
    const std::size_t lineNumberColumn =
        LineNumberGutterActive() ? (kLineNumberGap + std::to_string(totalLines).size() + kLineNumberGap) : 0;
    return dapColumn + diffColumn + kStatusWidth + kDiagnosticWidth + lineNumberColumn + testColumn + coverageColumn +
           symbolColumn + foldColumn + blameColumn;
}

bool BufferView::SymbolGutterActive() const {
    EnsureSymbolGutterCache();
    return !symbolGutterLineKinds_.empty();
}

bool BufferView::TestGutterActive() const {
    EnsureTestGutterCache();
    return !testGutterEntries_.empty();
}

std::size_t BufferView::TestGutterColumnStart() const {
    // Mirrors Paint()'s own left-to-right column sum
    // ([dap][diff][status][diagnostic][gap][digits][gap][test]) rather than
    // the fold-click's subtract-from-the-right trick -- that one works only
    // because fold and blame are the two rightmost regions, which the test
    // column is not.
    const std::size_t dapColumnWidth     = DapGutterActive() ? kDapWidth : 0;
    const std::size_t diffColumnWidth    = DiffGutterActive() ? kDiffWidth : 0;
    const std::size_t lineNumberGapWidth = LineNumberGutterActive() ? kLineNumberGap : 0;
    const std::size_t gutterDigits =
        LineNumberGutterActive() ? std::to_string(activeBuffer_.Get().Content().LineCount()).size() : 0;
    return dapColumnWidth + diffColumnWidth + kStatusWidth + kDiagnosticWidth + lineNumberGapWidth + gutterDigits +
           lineNumberGapWidth;
}

bool BufferView::CoverageGutterActive() const {
    EnsureCoverageGutterCache();
    return !coverageGutterLineStatuses_.empty();
}

bool BufferView::DiffGutterActive() const {
    return !diffLineKinds_.empty();
}

bool BufferView::LineNumberGutterActive() const {
    return editor::multibuffer::MultibufferIndexFor(activeBuffer_.Get()) == nullptr;
}

bool BufferView::DapGutterActive() const {
    if (dapManager_ == nullptr) {
        return false;
    }
    EnsureDapPathKey();
    if (dapPathKey_.empty()) {
        return false; // a pathless buffer can't hold breakpoints or be stopped in
    }
    if (!dapManager_->BreakpointLinesForKey(dapPathKey_).empty()) {
        return true;
    }
    const auto stop = dapManager_->CurrentStopKeyAndLine();
    return stop && stop->first == dapPathKey_;
}

void BufferView::EnsureDapPathKey() const {
    const text::Buffer& buffer = activeBuffer_.Get();
    if (dapPathKeyBuffer_ == &buffer && dapPathKeyRawPath_ == buffer.Path()) {
        return;
    }
    dapPathKeyBuffer_  = &buffer;
    dapPathKeyRawPath_ = buffer.Path();
    dapPathKey_        = buffer.Path() ? editor::dap::DapManager::NormalizePathKey(*buffer.Path()) : std::string();
}

void BufferView::EnsureTopLineValidForActiveBuffer() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (topLineValidatedBuffer_ == &buffer) {
        return;
    }
    topLineValidatedBuffer_ = &buffer;
    DismissHover(); // hover-tooltips follow-up: a tooltip from the previous buffer means nothing here
    // session-persistence slice 1: a stored viewport for this buffer wins
    // over whatever topLine_ the previous buffer left behind -- this seam
    // fires exactly once per buffer switch (and on a pane's very first
    // Paint, covering the startup buffer), which is what makes restored
    // scroll positions land here and nowhere else. Clamped by MaxTopLine()
    // against content that shrank since the place was recorded (an outside
    // edit between runs); ScrollToShowPoint() below then still guarantees
    // point is visible, so a topLine/point pair that somehow disagrees
    // resolves in point's favor, never a blank or point-less view.
    if (const auto place = editor::StoredFilePlaceFor(buffer); place && place->topLine) {
        topLine_ = std::min(*place->topLine, MaxTopLine());
    }
    else {
        // Real reported bug (LSP log buffer): topLine_ left behind by a
        // much longer previous buffer (say line 50) could exceed this
        // buffer's own MaxTopLine() (0, if its whole handful of lines
        // fits in one viewport). ScrollToShowPoint()'s "point is above
        // topLine_" branch below then set topLine_ = pointLine exactly --
        // pinning point to the viewport's literal top row and leaving
        // every row beneath it blank, instead of clamping down to 0 and
        // showing the whole short buffer with point at its natural
        // (bottom) position. Clamping here first preserves the "leave
        // topLine_ alone when it already suits the new buffer" case just
        // below (topLine_ <= MaxTopLine() already, so this is a no-op)
        // while fixing the case where it doesn't.
        topLine_ = std::min(topLine_, MaxTopLine());
    }
    // ScrollToShowPoint() alone (no need to reset topLine_ to 0 first) is
    // already safe against topLine_ being an arbitrary leftover value from
    // whichever buffer was active before: its own "point is above topLine_"
    // branch fires unconditionally whenever pointLine < topLine_, which is
    // exactly what happens when topLine_ was left pointing well past the
    // newly active (and possibly much shorter) buffer's own last line --
    // this buffer's own pointLine is always a real, in-range line, so it's
    // always < an out-of-range topLine_. The one thing this deliberately
    // preserves rather than discarding: if topLine_ happens to already show
    // the new buffer's own point (e.g. switching between two similarly
    // long buffers), it's left exactly where it was instead of always
    // jumping back to the top.
    ScrollToShowPoint();
}

void BufferView::ScrollToShowPoint() {
    ScrollToShowOffset(activeBuffer_.Get().Point());
    ScrollToShowPointHorizontally();
}

void BufferView::ScrollToShowOffset(std::size_t offset) {
    const text::ITextStorage& content   = activeBuffer_.Get().Content();
    const std::size_t pointLine = content.ByteOffsetToLine(offset);

    if (pointLine < topLine_) {
        topLine_ = pointLine;
    }
    else if (size().height > 0) {
        // main-editor-sticky-scroll follow-up: same stickyRowCount_
        // deduction MaxTopLine() applies now -- see its own doc comment.
        const auto        visibleLines  = static_cast<std::size_t>(std::max(0, size().height - stickyRowCount_));
        const std::size_t pointLineRows = RowsForLine(pointLine);
        // Org-mode fold/unfold follow-up: was `pointLine >= topLine_ +
        // visibleLines` / `topLine_ = pointLine - visibleLines + 1`, raw
        // buffer-line arithmetic that assumed every line between topLine_
        // and pointLine renders as its own row. line-wrap follow-up:
        // VisibleRowCountAtLeast is the fold-AND-wrap-aware "would
        // pointLine's own last row still fit" check (was
        // VisibleLineCountBetween, then VisibleRowCountBetween -- an
        // early-exit bounded check now, not an exact sum, so this never
        // walks more of a huge document than the viewport itself needs;
        // see VisibleRowCountAtLeast's own doc comment).
        // pointLineRows > visibleLines is checked first, short-circuiting
        // before the subtraction below could ever underflow (pointLine
        // itself taller than the whole viewport -- always needs a rescroll
        // regardless of what's above it).
        if (pointLineRows > visibleLines || VisibleRowCountAtLeast(topLine_, pointLine, visibleLines + 1 - pointLineRows)) {
            // line-wrap follow-up: was a partial-credit backward walk
            // (`remaining -= min(remaining, RowsForLine(newTop))`) that
            // could stop mid-line, silently discarding whatever didn't
            // fit -- topLine_ can only ever start at a line's own first
            // row (a documented scope cut, not mid-segment), so
            // "partially fitting" a line here doesn't correspond to
            // anything Paint() can actually render: it draws that line's
            // FULL row count regardless. Now walks backward including only
            // WHOLE lines that still fit alongside pointLine's own (always
            // fully included) rows, stopping before, not mid-way through,
            // one that wouldn't.
            std::size_t newTop      = pointLine;
            std::size_t accumulated = pointLineRows;
            while (newTop > 0) {
                const std::size_t candidate = newTop - 1;
                if (IsLineHidden(candidate)) {
                    newTop = candidate;
                    continue;
                }
                const std::size_t rows = RowsForLine(candidate);
                if (accumulated + rows > visibleLines) {
                    break;
                }
                accumulated += rows;
                newTop = candidate;
            }
            topLine_ = newTop;
        }
    }
    // multi-cursor-round-2 follow-up: no ScrollToShowPointHorizontally()
    // call here -- it always reads activeBuffer_.Get().Point() directly, so
    // calling it for an arbitrary secondary-cursor offset would scroll
    // horizontally to the *primary's* column instead, wrong for exactly the
    // case this parameterized form exists for. ScrollToShowPoint() (below)
    // still gets both, since it always wants "make sure point itself is
    // fully visible."
}

void BufferView::ScrollToShowPointHorizontally() {
    if (EffectiveWrapLines()) {
        return; // a wrapped line never extends past the viewport width -- nothing to scroll
    }

    const text::Buffer& buffer      = activeBuffer_.Get();
    const text::ITextStorage&   content     = buffer.Content();
    const std::size_t   point       = buffer.Point();
    const std::size_t   pointLine   = content.ByteOffsetToLine(point);
    const std::size_t   lineStart   = content.LineToByteOffset(pointLine);
    const std::size_t   gutterWidth = GutterWidth();
    const Size          sizeNow     = size();
    if (sizeNow.width <= 0) {
        return; // nothing meaningful to clamp against yet (e.g. before the first real layout)
    }
    const int contentWidth = std::max(1, sizeNow.width - static_cast<int>(gutterWidth));

    EnsureLinkCache();
    const std::size_t lineEnd =
        (pointLine + 1 < content.LineCount()) ? content.LineToByteOffset(pointLine + 1) - 1 : content.ByteLength();
    const std::vector<RenderedLink> lineLinks = LinksForLine(linkCache_, lineStart, lineEnd, point);

    // Point's true column from the start of the line, unbounded (well,
    // bounded only by the line's own length, not the viewport) -- needed to
    // decide whether leftColumn_ has to move at all, so this can't reuse
    // VisualColumn's own maxColumns-bounded form directly; a pathologically
    // long line still only walks as far as point itself, same cost class as
    // every other per-line scan in this file.
    const std::optional<int> visualCol =
        VisualColumn(content, lineStart, point, std::numeric_limits<int>::max(), lineLinks);
    if (!visualCol) {
        return; // shouldn't happen with an unbounded maxColumns, but a safe no-op
    }

    if (*visualCol < static_cast<int>(leftColumn_)) {
        leftColumn_ = static_cast<std::size_t>(*visualCol);
    }
    else if (*visualCol >= static_cast<int>(leftColumn_) + contentWidth) {
        leftColumn_ = static_cast<std::size_t>(*visualCol - contentWidth + 1);
    }
}

std::size_t BufferView::TopLine() const {
    return topLine_;
}

void BufferView::SetTopLine(std::size_t line) {
    const auto [rangeStart, rangeEnd] = NarrowedLineRange();
    // Org-mode fold/unfold follow-up: topLine_ must always land on a
    // visible line (every other fold-aware query here assumes that), so a
    // target sitting inside a hidden range rounds forward to the next
    // visible one before the usual clamp.
    line     = NextVisibleLine(std::max(line, rangeStart), rangeEnd);
    topLine_ = std::clamp(line, rangeStart, MaxTopLine());
}

std::size_t BufferView::LeftColumn() const {
    return leftColumn_;
}

void BufferView::SetLeftColumn(std::size_t column) {
    leftColumn_ = column;
}

bool BufferView::EffectiveWrapLines() const {
    return editor::EffectiveWrapLines(activeBuffer_.Get().Path(), activeBuffer_.Get().Name(), mode_);
}

std::pair<std::size_t, std::size_t> BufferView::NarrowedLineRange() const {
    const text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.IsNarrowed()) {
        return {0, buffer.Content().LineCount()};
    }
    const auto [narrowedStart, narrowedEnd] = buffer.NarrowedRange();
    const std::size_t startLine             = buffer.Content().ByteOffsetToLine(narrowedStart);
    // narrowedEnd is a byte offset at a line's own start (see
    // Buffer::NarrowToRegion's whole-line-snapping doc comment) -- except
    // when it's the buffer's own real end, which may fall mid-line. Either
    // way, the line *containing* the byte just before it is the narrowed
    // range's own last line; +1 makes this exclusive, matching
    // Content().LineCount()'s own convention.
    const std::size_t endLine = buffer.Content().ByteOffsetToLine(narrowedEnd > 0 ? narrowedEnd - 1 : 0) + 1;
    return {startLine, endLine};
}

std::size_t BufferView::MaxTopLine() const {
    const auto [rangeStart, rangeEnd] = NarrowedLineRange();
    // main-editor-sticky-scroll follow-up: the pinned rows from the last
    // Paint() eat into the pane's own real, drawable body height the exact
    // same way CursorPosition()/ByteOffsetForPoint already account for --
    // omitting this here undercounts how many rows the sticky band is
    // currently costing, so this method thought more content fit below
    // topLine_ than Paint() could actually draw, permanently stranding a
    // wrapped document's true last lines just past the bottom of the
    // viewport. One-frame-stale like every other stickyRowCount_ read
    // (Paint() recomputes it fresh every frame, so this self-corrects).
    const auto visibleLines =
        size().height > 0 ? static_cast<std::size_t>(std::max(0, size().height - stickyRowCount_)) : 0;
    // Org-mode fold/unfold follow-up: was `rangeStart + (totalLines >
    // visibleLines ? totalLines - visibleLines : 0)`, plain buffer-line
    // subtraction. line-wrap follow-up: VisibleRowCountAtLeast is the
    // fold-AND-wrap-aware "does everything already fit in one viewport"
    // check (was VisibleLineCountBetween, then an exact-sum
    // VisibleRowCountBetween -- an early-exit bounded check now, so this
    // never walks more of a huge document than the viewport itself could
    // need; see that method's own doc comment). Checking "at least
    // visibleLines + 1" is the negation of "the total is <= visibleLines."
    if (!VisibleRowCountAtLeast(rangeStart, rangeEnd, visibleLines + 1)) {
        return rangeStart;
    }
    // line-wrap follow-up: was a partial-credit backward walk
    // (`remaining -= min(remaining, RowsForLine(newTop))`) that could stop
    // mid-line, silently discarding whatever didn't fit -- topLine_ can
    // only ever start at a line's own first row (a documented scope cut,
    // not mid-segment), so "partially fitting" a line here doesn't
    // correspond to anything Paint() can actually render: it draws that
    // line's FULL row count regardless of how much of it "fit" in this
    // walk's own bookkeeping, which could silently push whatever comes
    // after it off the bottom of the viewport entirely. A real, reported
    // bug this fixes: scrolling to the end of a wrapped document could
    // leave its own last lines permanently unreachable, since a wrapped
    // line earlier in the walk could swallow the whole remaining budget
    // without actually being fully shown. Now walks backward including
    // only WHOLE lines that still fit within the budget, stopping before
    // (not mid-way through) one that wouldn't -- except the very first
    // real line considered, which is always included in full even if it
    // alone exceeds visibleLines (matching "always show at least the last
    // line" -- the same guarantee this walk already gave for free back
    // when every line was implicitly exactly 1 row).
    std::size_t newTop      = rangeEnd;
    std::size_t accumulated = 0;
    while (newTop > rangeStart) {
        const std::size_t candidate = newTop - 1;
        if (IsLineHidden(candidate)) {
            newTop = candidate;
            continue;
        }
        const std::size_t rows = RowsForLine(candidate);
        if (accumulated > 0 && accumulated + rows > visibleLines) {
            break;
        }
        accumulated += rows;
        newTop = candidate;
    }
    return newTop;
}

} // namespace ned::ui
