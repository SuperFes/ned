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

#include <cstddef>
#include <utility>
#include <vector>

#include "Text/Buffer.h"
#include "Text/ConflictHunk.h"
#include "UI/BufferView/CacheStamp.h"
#include "UI/BufferView/EditorContext.h"

namespace ned::ui::bufferview {

class GutterModel {
  public:
    explicit GutterModel(EditorContext& context) : context_(context) {
    }

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

  private:
    void EnsureUnsavedChanges() const;
    void EnsureDiagnosticSeverities() const;
    void EnsureConflictHunks() const;

    EditorContext& context_;

    mutable CacheStamp                                       unsavedChangeStamp_;
    mutable std::vector<std::pair<std::size_t, std::size_t>> unsavedChangeLineRanges_;

    mutable CacheStamp                                                              diagnosticStamp_;
    mutable std::vector<std::pair<std::size_t, text::Buffer::Diagnostic::Severity>> diagnosticLineSeverities_;

    mutable CacheStamp                      conflictHunkStamp_;
    mutable std::vector<text::ConflictHunk> conflictHunks_;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_GUTTERMODEL_H
