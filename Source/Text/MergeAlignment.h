//
// Line correspondence between a merged text and the sides it was merged
// from, and the blank rows each pane of a side-by-side view needs so that
// corresponding lines share a screen row. Pure and buffer-free, like
// ConflictHunk.h.
//
// A side is its whole text plus a chunk list tiling both it and the merged
// text in order. Alignment works from chunks alone, so a side can come from
// anywhere that can say which lines correspond: MergeSideFromMarkers derives
// one from conflict markers, and a full original file diffed against the
// merged text (DiffLines) yields the same shape.
//

#ifndef NED_TEXT_MERGEALIGNMENT_H
#define NED_TEXT_MERGEALIGNMENT_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ConflictHunk.h"

namespace ned::text {

// Merged lines [mergedLine, mergedLine + mergedCount) correspond to side
// lines [sideLine, sideLine + sideCount). An unchanged chunk has equal
// counts and identical lines; either count of a changed chunk may be 0.
struct AlignedChunk {
    std::size_t mergedLine;
    std::size_t mergedCount;
    std::size_t sideLine;
    std::size_t sideCount;
    bool        changed;

    bool operator==(const AlignedChunk&) const = default;
};

struct MergeSide {
    std::string               text;
    std::size_t               lineCount = 0; // SplitLines(text).size()
    std::vector<AlignedChunk> chunks;
};

enum class MergeSideKind { Ours,
                           Base,
                           Theirs };

// The merged text with every hunk's marker block replaced by that side's
// section; each hunk becomes one changed chunk. Base is nullopt unless every
// hunk carries a diff3 base section.
[[nodiscard]] std::optional<MergeSide> MergeSideFromMarkers(std::string_view merged, std::span<const ConflictHunk> hunks,
                                                            MergeSideKind kind);

// Where each line of one pane lands once blank rows are inserted. Rows are
// counted in unwrapped lines -- alignment assumes no soft wrap.
class PaneAlignment {
  public:
    PaneAlignment() = default;
    explicit PaneAlignment(std::vector<std::size_t> rowOfLine);

    // Blank rows painted before line 0.
    [[nodiscard]] std::size_t LeadingRows() const;
    // Blank rows painted after `line`; 0 past the end.
    [[nodiscard]] std::size_t RowsAfter(std::size_t line) const;
    // Aligned row of `line`, clamped to the total row count past the end.
    [[nodiscard]] std::size_t AlignedRow(std::size_t line) const;
    // The line painted at `row`, or the last line before it when `row` is a
    // blank row; 0 for rows before line 0.
    [[nodiscard]] std::size_t LineAtAlignedRow(std::size_t row) const;
    [[nodiscard]] std::size_t TotalRows() const;
    [[nodiscard]] std::size_t LineCount() const;

    // A pane's scroll position: its top line, and blank rows painted above
    // that line so the pane can start partway through the alignment rows
    // after the line before it.
    struct Top {
        std::size_t line    = 0;
        std::size_t padding = 0;

        bool operator==(const Top&) const = default;
    };
    // The aligned row painted at the top of a pane scrolled to `top`.
    [[nodiscard]] std::size_t RowAtTop(Top top) const;
    // The scroll position that paints aligned row `row` at the top. A row
    // inside the leading rows before line 0 can't be reached exactly and
    // settles on line 0.
    [[nodiscard]] Top TopForRow(std::size_t row) const;

  private:
    // Size lineCount + 1; the sentinel is the total row count.
    std::vector<std::size_t> rowOfLine_ = {0};
};

// [0] aligns the merged pane, [1 + i] aligns sides[i]. Each pane is padded
// to the tallest pane between consecutive lines every pane agrees on.
[[nodiscard]] std::vector<PaneAlignment> AlignPanes(std::size_t mergedLineCount, std::span<const MergeSide* const> sides);

} // namespace ned::text

#endif // NED_TEXT_MERGEALIGNMENT_H
