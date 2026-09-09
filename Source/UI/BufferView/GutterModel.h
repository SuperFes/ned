//
// The per-line data the gutter columns render, derived from the buffer and
// memoised -- see Docs/BufferViewDecomposition.md.
//
// Each column (status, diagnostics, folds, symbols, tests, coverage, blame,
// diff, conflicts) needs the same shape of answer: "for line N, what should be
// drawn here?" Computing that per row per frame is far too slow, so each is
// derived once and kept until the thing it was derived from changes -- which is
// what CacheStamp tracks. This class owns those derivations and the state
// behind them; the renderer only reads the results.
//
// Accessors derive on demand: there is no separate "refresh first" step a caller
// could forget, and a repeat call inside the same frame is a stamp comparison.
// Everything here is const and the state is mutable, because deriving a cached
// value is not a logical change to the model.
//
// Per-pane, not per-process. Every WindowManager::Pane owns its own BufferView
// and therefore its own GutterModel: two split panes showing the same file
// still scroll independently and so window their structural queries
// differently. The genuinely process-wide inputs (whether folding is enabled at
// all, the huge-file window size, whether a test filter command is configured)
// are read from the mutex-guarded settings modules in Editor/ that already own
// them.
//

#ifndef NED_UI_BUFFERVIEW_GUTTERMODEL_H
#define NED_UI_BUFFERVIEW_GUTTERMODEL_H

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Editor/Coverage/Report.h"
#include "Editor/Mode.h"
#include "Editor/TestRun/TestResult.h"
#include "Text/Buffer.h"
#include "Text/ConflictHunk.h"
#include "Text/ITextStorage.h"
#include "UI/BufferView/CacheStamp.h"
#include "UI/BufferView/EditorContext.h"

namespace ned::ui::bufferview {

class GutterModel {
  public:
    // How much of a huge buffer the structural queries (folds, symbols, tests)
    // are allowed to look at, given the content they are about to parse.
    // Supplied rather than computed here because it depends on where the
    // viewport is, which is the view's business, not the gutter's -- and it
    // must be asked fresh each time, since scrolling moves it without any
    // generation counter changing. On an ordinary buffer it is the whole thing.
    using StructuralWindowFn = std::function<std::pair<std::size_t, std::size_t>(const text::ITextStorage&)>;

    GutterModel(EditorContext& context, StructuralWindowFn structuralWindow) : context_(context), structuralWindow_(std::move(structuralWindow)) {
    }

    // A fixed number of gutter columns are reserved for fold depth, rather than
    // a count that grows with how deeply the visible content happens to nest --
    // a deliberate choice, so the gutter's width never shifts while scrolling
    // past a deeply nested region. Blocks deeper than this get no affordance at
    // all; they stay foldable through code-fold-toggle.
    static constexpr int kMaxFoldDepthColumns = 4;

    struct FoldGutterEntry {
        std::size_t headerLine;
        std::size_t closerLine; // inclusive
        std::size_t blockStart; // FoldMarker key
        int         column;     // == depth; blocks at depth >= kMaxFoldDepthColumns get no entry at all
    };

    struct TestGutterEntry {
        std::size_t                                        line = 0;
        std::optional<editor::testrun::TestResult::Status> status; // nullopt = discovered but not run
        std::string                                        name;
    };

    struct InlineDiagnostic {
        text::Buffer::Diagnostic::Severity severity;
        std::size_t                        startByte;
        std::size_t                        endByte;
        std::string                        message;
    };

    GutterModel(const GutterModel&)            = delete;
    GutterModel& operator=(const GutterModel&) = delete;

    // Merged, sorted [startLine, endLineExclusive) ranges of lines edited since
    // the last load or save, for the status column. Derived from
    // Buffer::UnsavedChangeRanges() and keyed on both ContentGeneration() and
    // UnsavedChangeGeneration(): an edit bumps both, but a save bumps only the
    // latter, clearing the ranges without otherwise touching content.
    //
    // Flat and disjoint by construction -- there is no nesting here the way
    // there is for fold depth -- so rendering a row is a binary search with no
    // streaming state.
    [[nodiscard]] const std::vector<std::pair<std::size_t, std::size_t>>& UnsavedChangeLineRanges() const;

    // At most one {line, severity} entry per line, sorted by line, for the
    // diagnostic column. A diagnostic's range can span lines, but the gutter
    // marks only the line it *starts* on -- the one-glyph-per-line convention
    // most editors' diagnostic gutters use -- and the most severe diagnostic
    // starting on a line wins.
    //
    // Keyed on DiagnosticsGeneration() alone: SetDiagnostics always replaces
    // the set wholesale, so unlike fold markers there is nothing incrementally
    // relocated across edits that a content generation would have to catch.
    [[nodiscard]] const std::vector<std::pair<std::size_t, text::Buffer::Diagnostic::Severity>>&
    DiagnosticLineSeverities() const;

    // The buffer's merge-conflict hunks, for conflict tinting and the
    // resolution commands. Keyed on ContentGeneration() alone and never
    // windowed: a conflicted file is an ordinary source file in practice, and
    // re-scanning for markers is a cheap linear pass regardless.
    [[nodiscard]] const std::vector<text::ConflictHunk>& ConflictHunks() const;

    // --- folds ---------------------------------------------------------------
    // Whether the fold column is drawn at all: the mode has a real fold query,
    // the feature is enabled process-wide, and the buffer is writable.
    [[nodiscard]] bool FoldGutterActive() const;
    // The foldable byte ranges themselves, windowed on a huge buffer.
    [[nodiscard]] const std::vector<std::pair<std::size_t, std::size_t>>& FoldableBlocks() const;
    // One entry per drawable fold, sorted by header line.
    [[nodiscard]] const std::vector<FoldGutterEntry>& FoldEntries() const;
    // Per column, the [headerLine + 1, closerLine + 1) spans of *expanded*
    // blocks, for drawing each column's guide line.
    [[nodiscard]] const std::array<std::vector<std::pair<std::size_t, std::size_t>>, kMaxFoldDepthColumns>&
    FoldLineRangesByColumn() const;

    // --- symbols -------------------------------------------------------------
    // Data-driven, unlike the fold column: this appears only when the mode's
    // query actually produced markers.
    [[nodiscard]] bool                                                           SymbolGutterActive() const;
    [[nodiscard]] const std::vector<editor::SymbolMarker>&                       SymbolMarkers() const;
    [[nodiscard]] const std::vector<std::pair<std::size_t, editor::SymbolKind>>& SymbolLineKinds() const;

    // --- tests ---------------------------------------------------------------
    [[nodiscard]] bool                                TestGutterActive() const;
    [[nodiscard]] const std::vector<TestGutterEntry>& TestEntries() const;
    // Whether a discovered-but-unrun test should show a clickable run
    // affordance, which is only meaningful once a filter command is configured.
    [[nodiscard]] bool TestRunnable() const;

    // --- coverage ------------------------------------------------------------
    [[nodiscard]] bool                                                                     CoverageGutterActive() const;
    [[nodiscard]] const std::vector<std::pair<std::size_t, editor::coverage::LineStatus>>& CoverageLineStatuses() const;

    // --- inline diagnostics --------------------------------------------------
    [[nodiscard]] const std::unordered_map<std::size_t, InlineDiagnostic>& InlineDiagnosticsByLine() const;

    // Drop everything remembered about a buffer that is going away, including
    // what is kept for it across buffer switches.
    void ForgetBuffer(text::Buffer& buffer);

    // Discard the caches whose contents depend on the active Mode. The view
    // calls this when it notices the mode changed under a buffer switch, before
    // anything can stamp them current under the old mode.
    void InvalidateModeDependentCaches();

  private:
    void EnsureUnsavedChanges() const;
    void EnsureDiagnosticSeverities() const;
    void EnsureConflictHunks() const;
    void EnsureFoldableBlocks() const;
    void EnsureFoldEntries() const;
    void EnsureSymbolMarkers() const;
    void EnsureSymbolLineKinds() const;
    void EnsureTestEntries() const;
    void EnsureCoverageStatuses() const;
    void EnsureInlineDiagnostics() const;

    EditorContext&     context_;
    StructuralWindowFn structuralWindow_;

    mutable CacheStamp                                       unsavedChangeStamp_;
    mutable std::vector<std::pair<std::size_t, std::size_t>> unsavedChangeLineRanges_;

    mutable CacheStamp                                                              diagnosticStamp_;
    mutable std::vector<std::pair<std::size_t, text::Buffer::Diagnostic::Severity>> diagnosticLineSeverities_;

    mutable CacheStamp                      conflictHunkStamp_;
    mutable std::vector<text::ConflictHunk> conflictHunks_;

    mutable CacheStamp foldableBlocksStamp_;
    // The window the blocks were last derived for. The fold-entry cache keys off
    // it, since a window-only change moves the blocks with no content or fold
    // edit for a generation counter to catch.
    mutable std::pair<std::size_t, std::size_t>              foldableBlocksWindow_{0, 0};
    mutable std::vector<std::pair<std::size_t, std::size_t>> foldableBlocks_;

    // Kept per buffer as well as for the active one, so switching away and back
    // does not re-run the fold query. Keyed on everything that would change the
    // answer, the window included.
    struct FoldableBlocksEntry {
        std::size_t                                      contentGeneration = 0;
        std::string                                      modeName;
        std::size_t                                      windowStart = 0;
        std::size_t                                      windowEnd   = 0;
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
    };
    mutable std::unordered_map<text::Buffer*, FoldableBlocksEntry> foldableBlocksByBuffer_;

    mutable CacheStamp                                                                         foldEntriesStamp_;
    mutable std::vector<FoldGutterEntry>                                                       foldEntries_;
    mutable std::array<std::vector<std::pair<std::size_t, std::size_t>>, kMaxFoldDepthColumns> foldLineRangesByColumn_;

    mutable CacheStamp                          symbolMarkersStamp_;
    mutable std::pair<std::size_t, std::size_t> symbolMarkersWindow_{0, 0};
    mutable std::vector<editor::SymbolMarker>   symbolMarkers_;

    mutable CacheStamp                                              symbolLineKindsStamp_;
    mutable std::vector<std::pair<std::size_t, editor::SymbolKind>> symbolLineKinds_;

    mutable CacheStamp                   testEntriesStamp_;
    mutable bool                         testRunnable_ = false;
    mutable std::vector<TestGutterEntry> testEntries_;

    mutable CacheStamp                                                        coverageStamp_;
    mutable std::vector<std::pair<std::size_t, editor::coverage::LineStatus>> coverageLineStatuses_;

    mutable CacheStamp                                        inlineDiagnosticStamp_;
    mutable std::unordered_map<std::size_t, InlineDiagnostic> inlineDiagnosticsByLine_;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_GUTTERMODEL_H
