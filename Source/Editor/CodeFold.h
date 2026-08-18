//
// Generic, tree-sitter-backed code folding for ordinary source buffers --
// the non-Org counterpart to Org.h's headline-based fold/unfold (see that
// file's own FoldedLineRanges/CycleFoldAtPoint). Deliberately much simpler:
// there's no outline/tree structure here, no 3-state cycle, just "does a
// foldable block (as reported by the active Mode's fold query, see Mode.h)
// start on this line, and is it currently collapsed."
//
// Reuses Buffer's existing generic FoldMarker/FoldMarkers_ storage
// unchanged -- only FoldMarker::Collapsed is ever used here (a plain binary
// fold, not Org's Collapsed/ChildrenVisible cycle), keyed by a foldable
// block's own startByte, the same "generic position bookkeeping" role
// FoldMarkers_ already plays for Org's headline byte offsets.
//

#ifndef NED_EDITOR_CODEFOLD_H
#define NED_EDITOR_CODEFOLD_H

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "Mode.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

namespace ned::editor::codefold {

// Calls mode.fold(bufferText) if set, else returns {} -- the only function
// here that touches Mode::fold. Callers (BufferView) are expected to cache
// this result themselves rather than call it fresh on every use, the same
// way BufferView already caches mode.highlight's result.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> FoldableBlocks(const Mode&      mode,
                                                                               std::string_view bufferText);

// Hidden [startLine, endLineExclusive) ranges for every FoldMarker::Collapsed
// entry in buffer.FoldMarkers() whose key matches one of `blocks`' own
// startByte -- `blocks` is expected to be FoldableBlocks' own (sorted)
// result for the same buffer/mode. Mirrors org::FoldedLineRanges' return
// shape exactly (same [start, end) hides-through-the-closing-line
// convention Org's own Collapsed marker already established -- see that
// file's own doc comment) so BufferView can treat both uniformly. A stale
// marker -- one whose byte offset no longer matches any block in `blocks`,
// e.g. because the buffer's content changed since it was set -- is silently
// skipped, not an error, the same stale-state tolerance Org's own
// FoldedLineRanges already extends to a marker that no longer lands on a
// real headline.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>>
FoldedLineRanges(const text::Buffer& buffer, const text::Rope& content,
                 const std::vector<std::pair<std::size_t, std::size_t>>& blocks);

// Toggles the fold marker for whichever block in `blocks` starts on `line`
// (its opening/header line -- e.g. the line with a function's `{`) --
// innermost (smallest byte range) wins if more than one block happens to
// open on the same line. Returns false (a no-op) if no block starts there.
bool ToggleFoldAtLine(text::Buffer& buffer, const text::Rope& content,
                      const std::vector<std::pair<std::size_t, std::size_t>>& blocks, std::size_t line);

// depth-aware-fold-gutter follow-up: a block's own nesting depth, 0 =
// outermost. Uncapped here -- BufferView caps for display (a small fixed
// number of gutter columns, not a viewport-dependent width).
struct FoldRegion {
    std::size_t startByte;
    std::size_t endByte;
    int         depth;
};

// `blocks` must be sorted by (startByte, endByte) ascending -- exactly what
// FoldableBlocks (and Mode::fold, which sorts its own output) already
// produce. A real O(n) stack-based interval-nesting-depth walk: pop any
// still-open ancestor whose own endByte has already passed this block's
// startByte, this block's depth is however many ancestors are still open,
// then push this block as a new open ancestor for whatever comes next.
// Well-defined because every block byte range here comes from a real
// tree-sitter AST node (each fold query's own "@fold" captures) -- two such
// ranges are always either disjoint or properly nested, never partially
// overlapping.
[[nodiscard]] std::vector<FoldRegion> FoldRegionsWithDepth(const std::vector<std::pair<std::size_t, std::size_t>>& blocks);

} // namespace ned::editor::codefold

#endif // NED_EDITOR_CODEFOLD_H
