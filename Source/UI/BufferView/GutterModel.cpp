#include "UI/BufferView/GutterModel.h"

#include <algorithm>
#include <unordered_map>

#include <string>

#include "Editor/CodeFold.h"
#include "Editor/CodeFoldSettings.h"
#include "Editor/Coverage/Config.h"
#include "Editor/HugeStructuralWindow.h"
#include "Editor/InlineDiagnostics.h"
#include "Editor/ProjectRoot.h"
#include "Editor/TestRun/Config.h"
#include "Editor/TestRun/TestRunner.h"
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

void GutterModel::EnsureFoldableBlocks() const {
    text::Buffer& buffer = context_.activeBuffer.Get();

    if (!FoldGutterActive()) {
        foldableBlocks_.clear();
        // Deliberately a narrower key than the eligible path's below: the two
        // never compare equal, so re-enabling the fold gutter always rebuilds
        // rather than matching a stamp left behind while the cache was empty.
        foldableBlocksStamp_ = CacheStamp::For(&buffer, {buffer.ContentGeneration()});
        return;
    }

    const text::ITextStorage& content   = buffer.Content();
    const bool                huge      = content.IsHuge();
    const auto [windowStart, windowEnd] = structuralWindow_(content);

    const CacheStamp stamp =
        CacheStamp::For(&buffer, {buffer.ContentGeneration(), windowStart, windowEnd});
    if (foldableBlocksStamp_.Matches(stamp)) {
        return;
    }

    // per-buffer-highlight-cache follow-up: persists across a buffer
    // switch -- see foldableBlocksByBuffer_'s own doc comment in
    // BufferView.h, and highlightCacheByBuffer_'s for the full reasoning
    // this mirrors.
    const auto it = foldableBlocksByBuffer_.find(&buffer);
    if (it == foldableBlocksByBuffer_.end() || it->second.contentGeneration != buffer.ContentGeneration() ||
        it->second.modeName != context_.mode.name || it->second.windowStart != windowStart || it->second.windowEnd != windowEnd) {
        FoldableBlocksEntry entry;
        // huge-file-structural-gutters follow-up: a huge buffer feeds
        // context_.mode.fold a bounded window (content.Substring) instead of the
        // whole document.
        entry.ranges = huge ? editor::codefold::FoldableBlocks(context_.mode, content.Substring(windowStart, windowEnd - windowStart))
                            : editor::codefold::FoldableBlocks(context_.mode, buffer.Text());
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
            const std::size_t windowLength       = windowEnd - windowStart;
            const bool        reachedDocumentEnd = windowEnd >= content.ByteLength();
            std::erase_if(entry.ranges, [&](const auto& range) { return !reachedDocumentEnd && range.second >= windowLength; });
            for (auto& [start, end] : entry.ranges) {
                start += windowStart;
                end += windowStart;
            }
        }
        entry.contentGeneration = buffer.ContentGeneration();
        entry.modeName          = context_.mode.name;
        entry.windowStart       = windowStart;
        entry.windowEnd         = windowEnd;
        foldableBlocks_         = entry.ranges;
        foldableBlocksByBuffer_.insert_or_assign(&buffer, std::move(entry));
    }
    else {
        foldableBlocks_ = it->second.ranges;
    }
    foldableBlocksStamp_  = stamp;
    foldableBlocksWindow_ = {windowStart, windowEnd};
}

void GutterModel::EnsureFoldEntries() const {
    EnsureFoldableBlocks();
    text::Buffer& buffer = context_.activeBuffer.Get();

    const CacheStamp stamp =
        CacheStamp::For(&buffer, {buffer.ContentGeneration(), buffer.FoldGeneration(),
                                  foldableBlocksWindow_.first, foldableBlocksWindow_.second});
    if (foldEntriesStamp_.Matches(stamp)) {
        return;
    }

    foldEntries_.clear();
    for (auto& column : foldLineRangesByColumn_) {
        column.clear();
    }

    if (!foldableBlocks_.empty()) {
        const text::ITextStorage& content = buffer.Content();
        const auto                regions = editor::codefold::FoldRegionsWithDepth(foldableBlocks_);

        foldEntries_.reserve(regions.size());
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
            if (!foldEntries_.empty() && foldEntries_.back().headerLine == headerLine) {
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
            foldEntries_.push_back(FoldGutterEntry{
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
                foldLineRangesByColumn_[column].emplace_back(headerLine + 1, closerLine + 1);
            }
        }
    }

    foldEntriesStamp_ = stamp;
}

void GutterModel::EnsureSymbolMarkers() const {
    text::Buffer& buffer = context_.activeBuffer.Get();

    // Eligibility gate -- mirrors FoldGutterActive's own context_.mode.fold/
    // ReadOnly() reasoning (a real query run against a synthesized
    // "path:line: text" results buffer produces meaningless markers, not an
    // empty result). Stamped as up to date even when ineligible so a repeat
    // call this same frame/buffer stays a cheap no-op.
    if (!context_.mode.symbolKind || buffer.ReadOnly()) {
        symbolMarkers_.clear();
        // Narrower key than the eligible path below, for the same reason
        // EnsureFoldableBlocks's own ineligible path is.
        symbolMarkersStamp_ = CacheStamp::For(&buffer, {buffer.ContentGeneration()});
        return;
    }

    const text::ITextStorage& content   = buffer.Content();
    const bool                huge      = content.IsHuge();
    const auto [windowStart, windowEnd] = structuralWindow_(content);

    const CacheStamp stamp =
        CacheStamp::For(&buffer, {buffer.ContentGeneration(), windowStart, windowEnd});
    if (symbolMarkersStamp_.Matches(stamp)) {
        return;
    }

    // huge-file-structural-gutters follow-up: a huge buffer feeds
    // context_.mode.symbolKind a bounded window instead of the whole document --
    // both startByte and endByte are then window-relative, remapped back to
    // absolute buffer coordinates (+= windowStart) here so every consumer
    // (the gutter below, sticky scroll) can treat this cache's coordinates
    // uniformly regardless of buffer size.
    symbolMarkers_ =
        huge ? context_.mode.symbolKind(content.Substring(windowStart, windowEnd - windowStart)) : context_.mode.symbolKind(buffer.Text());
    if (huge) {
        for (editor::SymbolMarker& marker : symbolMarkers_) {
            marker.startByte += windowStart;
            marker.endByte += windowStart;
        }
    }
    symbolMarkersStamp_  = stamp;
    symbolMarkersWindow_ = {windowStart, windowEnd};
}

void GutterModel::EnsureSymbolLineKinds() const {
    EnsureSymbolMarkers();
    text::Buffer& buffer = context_.activeBuffer.Get();

    const CacheStamp stamp =
        CacheStamp::For(&buffer, {buffer.ContentGeneration(), symbolMarkersWindow_.first,
                                  symbolMarkersWindow_.second});
    if (symbolLineKindsStamp_.Matches(stamp)) {
        return;
    }

    // symbolMarkers_ arrives sorted by startByte (Mode.cpp's own
    // closure) -- collapsing to one entry per line via a plain overwrite in
    // that order keeps the LAST (highest-byte-offset) marker on a line that
    // somehow has more than one, the same "later wins" convention
    // HighlightSpan's own doc comment establishes elsewhere in this file.
    const text::ITextStorage&                           content = buffer.Content();
    std::unordered_map<std::size_t, editor::SymbolKind> kindByLine;
    for (const editor::SymbolMarker& marker : symbolMarkers_) {
        kindByLine[content.ByteOffsetToLine(marker.startByte)] = marker.kind;
    }
    symbolLineKinds_.assign(kindByLine.begin(), kindByLine.end());
    std::sort(symbolLineKinds_.begin(), symbolLineKinds_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    symbolLineKindsStamp_ = stamp;
}

void GutterModel::EnsureTestEntries() const {
    text::Buffer& buffer = context_.activeBuffer.Get();

    // Eligibility gate, EnsureSymbolLineKinds's exact shape -- plus the
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
    if (!context_.mode.testDiscovery || buffer.ReadOnly() || context_.testRunner == nullptr) {
        testEntries_.clear();
        // Narrower key than the eligible path below, for the same reason
        // EnsureFoldableBlocks's own ineligible path is.
        testEntriesStamp_ = CacheStamp::For(
            &buffer, {buffer.ContentGeneration(), context_.testRunner != nullptr ? context_.testRunner->OutcomeGeneration() : 0});
        testRunnable_ = false; // no runner -- no affordance is possible either
        return;
    }
    const bool runnableAffordance = editor::testrun::HasTestFilterCommand();
    if (!context_.testRunner->LatestOutcome() && !runnableAffordance) {
        testEntries_.clear();
        testEntriesStamp_ =
            CacheStamp::For(&buffer, {buffer.ContentGeneration(), context_.testRunner->OutcomeGeneration()});
        testRunnable_ = runnableAffordance;
        return;
    }

    const text::ITextStorage& content   = buffer.Content();
    const bool                huge      = content.IsHuge();
    const auto [windowStart, windowEnd] = structuralWindow_(content);

    // testRunnable_ is part of the key, not just an input: a
    // filter command configured after a run has already landed changes what
    // rows exist without touching content, outcome, or window generation.
    const CacheStamp stamp = CacheStamp::For(
        &buffer, {buffer.ContentGeneration(), context_.testRunner->OutcomeGeneration(), windowStart, windowEnd,
                  static_cast<std::size_t>(runnableAffordance)});
    if (testEntriesStamp_.Matches(stamp)) {
        return;
    }

    // May hold nothing at all now (the runnable-affordance case above): the
    // marker loop then finds no matching result for any test and every row
    // comes out status-less, which is exactly the pre-run state.
    static const editor::testrun::Outcome kNoOutcome{};
    const editor::testrun::Outcome&       outcome =
        context_.testRunner->LatestOutcome() ? *context_.testRunner->LatestOutcome() : kNoOutcome;
    const std::string bufferBasename = buffer.Path() ? buffer.Path()->filename().string() : std::string();

    testEntries_.clear();
    // huge-file-structural-gutters follow-up: a huge buffer feeds
    // context_.mode.testDiscovery a bounded window instead of the whole document --
    // marker.startByte is then window-relative, remapped back to absolute
    // buffer coordinates (+= windowStart) before the ByteOffsetToLine call
    // below.
    for (const editor::TestMarker& marker :
         huge ? context_.mode.testDiscovery(content.Substring(windowStart, windowEnd - windowStart)) : context_.mode.testDiscovery(buffer.Text())) {
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
        if (!aggregate && outcome.failuresOnly && outcome.parsedOk && !context_.testRunner->LastRunWasFiltered()) {
            aggregate = editor::testrun::TestResult::Status::Passed;
        }
        // test-runner-gaps follow-up: a status-less row is kept (rather than
        // skipped as it used to be) only when the runnable affordance is on
        // -- that row paints '▸' and is what a gutter click runs. Without a
        // filter command configured, absence still means no row at all.
        if (aggregate || runnableAffordance) {
            testEntries_.push_back(TestGutterEntry{
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
    std::sort(testEntries_.begin(), testEntries_.end(), [&](const TestGutterEntry& a, const TestGutterEntry& b) {
        return a.line != b.line ? a.line < b.line : tieRank(a.status) < tieRank(b.status);
    });
    testEntries_.erase(
        std::unique(testEntries_.begin(), testEntries_.end(),
                    [](const TestGutterEntry& a, const TestGutterEntry& b) { return a.line == b.line; }),
        testEntries_.end());

    testEntriesStamp_ = stamp;
    testRunnable_     = runnableAffordance;
}

void GutterModel::EnsureCoverageStatuses() const {
    text::Buffer&     buffer           = context_.activeBuffer.Get();
    const std::size_t reportGeneration = editor::coverage::ReportGeneration();
    const CacheStamp  stamp            = CacheStamp::For(&buffer, {reportGeneration});
    if (coverageStamp_.Matches(stamp)) {
        return;
    }

    coverageLineStatuses_.clear();
    coverageStamp_ = stamp;

    if (!buffer.Path()) {
        return; // unsaved/scratch buffer -- nothing to match a coverage report's SF: path against
    }

    const editor::coverage::Report report = editor::coverage::CurrentCoverageReport();
    const editor::coverage::FileCoverage*  file =
        editor::coverage::FindFileCoverage(report, *buffer.Path(), editor::ProjectRoot());
    if (file == nullptr) {
        return;
    }

    coverageLineStatuses_.reserve(file->lines.size());
    for (const editor::coverage::LineCoverage& line : file->lines) {
        coverageLineStatuses_.emplace_back(line.line, line.Status());
    }
    // file->lines is already sorted-by-line/unique-per-line by construction
    // (OutputParser.h's own merge step keeps it that way), so no
    // sort/dedupe pass is needed here the way testEntries_ above
    // needs one (multiple test markers can share a line; coverage lines
    // can't).
}

void GutterModel::EnsureInlineDiagnostics() const {
    text::Buffer&    buffer = context_.activeBuffer.Get();
    const CacheStamp stamp =
        CacheStamp::For(&buffer, {buffer.DiagnosticsGeneration(), buffer.ContentGeneration()});
    if (inlineDiagnosticStamp_.Matches(stamp)) {
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
            SeverityRank(diagnostic.severity) > SeverityRank(it->second.severity) ||
            (SeverityRank(diagnostic.severity) == SeverityRank(it->second.severity) &&
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

    inlineDiagnosticStamp_ = stamp;
}

bool GutterModel::FoldGutterActive() const {
    return context_.mode.fold && editor::CodeFoldingEnabled() && !context_.activeBuffer.Get().ReadOnly();
}

bool GutterModel::SymbolGutterActive() const {
    EnsureSymbolLineKinds();
    return !symbolLineKinds_.empty();
}

bool GutterModel::TestGutterActive() const {
    EnsureTestEntries();
    return !testEntries_.empty();
}

bool GutterModel::CoverageGutterActive() const {
    EnsureCoverageStatuses();
    return !coverageLineStatuses_.empty();
}

bool GutterModel::TestRunnable() const {
    EnsureTestEntries();
    return testRunnable_;
}

const std::vector<std::pair<std::size_t, std::size_t>>& GutterModel::FoldableBlocks() const {
    EnsureFoldableBlocks();
    return foldableBlocks_;
}

const std::vector<GutterModel::FoldGutterEntry>& GutterModel::FoldEntries() const {
    EnsureFoldEntries();
    return foldEntries_;
}

const std::array<std::vector<std::pair<std::size_t, std::size_t>>, GutterModel::kMaxFoldDepthColumns>&
GutterModel::FoldLineRangesByColumn() const {
    EnsureFoldEntries();
    return foldLineRangesByColumn_;
}

const std::vector<editor::SymbolMarker>& GutterModel::SymbolMarkers() const {
    EnsureSymbolMarkers();
    return symbolMarkers_;
}

const std::vector<std::pair<std::size_t, editor::SymbolKind>>& GutterModel::SymbolLineKinds() const {
    EnsureSymbolLineKinds();
    return symbolLineKinds_;
}

const std::vector<GutterModel::TestGutterEntry>& GutterModel::TestEntries() const {
    EnsureTestEntries();
    return testEntries_;
}

const std::vector<std::pair<std::size_t, editor::coverage::LineStatus>>& GutterModel::CoverageLineStatuses() const {
    EnsureCoverageStatuses();
    return coverageLineStatuses_;
}

const std::unordered_map<std::size_t, GutterModel::InlineDiagnostic>& GutterModel::InlineDiagnosticsByLine() const {
    EnsureInlineDiagnostics();
    return inlineDiagnosticsByLine_;
}

void GutterModel::ForgetBuffer(text::Buffer& buffer) {
    foldableBlocksByBuffer_.erase(&buffer);
    if (foldableBlocksStamp_.IsFor(&buffer)) {
        foldableBlocksStamp_.Invalidate();
        foldableBlocks_.clear();
    }
}

void GutterModel::InvalidateModeDependentCaches() {
    // Both are derived through a Mode query, so a stamp left current under the
    // previous mode would hand back that mode's answers for this buffer.
    symbolLineKindsStamp_.Invalidate();
    testEntriesStamp_.Invalidate();
}

} // namespace ned::ui::bufferview
