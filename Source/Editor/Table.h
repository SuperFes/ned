//
// A small, format-agnostic table toolkit (tables follow-up): the piece of
// "parse a |-delimited grid, compute column widths, pad cells" logic that's
// identical whether the surrounding syntax is an Org table or a Markdown
// (GFM) one -- only what a *separator/delimiter* row means (Org: a plain
// visual hrule, no alignment info; GFM: mandatory, carries per-column
// alignment markers) differs, and that's each format's own job
// (Source/Editor/Org.h's FindOrgTableAtPoint/AlignOrgTableAtPoint,
// Source/Editor/Markdown.h's FindTableAtPoint/AlignTableAtPoint). Kept as
// its own file rather than folded into Org.h specifically so it carries no
// Org-specific assumption.
//
// Deliberately a handful of small, reusable functions, not a generic
// templated "table engine" -- matches this codebase's own established
// preference for small shared helpers (see Org.cpp's ReplaceOptionalToken)
// over building a framework ahead of a second, third, ... need.
//

#ifndef NED_EDITOR_TABLE_H
#define NED_EDITOR_TABLE_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ned::editor::table {

// Default pads the same as Left -- a distinct value so a caller can tell
// "no marker was present" (Org tables, or a GFM delimiter cell with no `:`)
// from "explicitly left-aligned" (GFM's `:---`) if it ever needs to.
enum class Alignment { Default,
                       Left,
                       Center,
                       Right };

// Splits one already-identified table-block line into trimmed cell text.
// Tolerates both "| a | b |" (edge pipes) and "a | b" (no edge pipes) --
// both are valid in real Org and real GFM -- but table *detection*
// (FindTableBlockLines below) still requires a leading `|`; a table line
// with no edge pipes at all is a rarer form this codebase doesn't try to
// auto-detect, a stated v1 simplification.
[[nodiscard]] std::vector<std::string> SplitRow(std::string_view line);

// Max display width per column across every row in dataRows (all rows must
// have already been split via SplitRow; ragged rows -- fewer cells than the
// widest row -- are tolerated, missing cells simply don't contribute to
// that column's width). Separator/delimiter rows carry no real content and
// must never be passed in here -- each format's own adapter renders those
// separately from the widths this computes.
//
// Width is a codepoint count, not a grapheme-cluster or East-Asian-Wide-
// aware display width -- a stated v1 simplification matching this
// codebase's own established "codepoint, not full Unicode width" cut in
// several other places (e.g. word motion's ASCII-only classification).
[[nodiscard]] std::vector<std::size_t> ComputeColumnWidths(const std::vector<std::vector<std::string>>& dataRows);

// Pads text to width columns per alignment (Default treated as Left).
// text wider than width is returned unchanged -- this never truncates.
[[nodiscard]] std::string PadCell(const std::string& text, std::size_t width, Alignment alignment);

// Cell *byte spans* (not text) for one already-identified table line,
// mirroring SplitRow's own trimming/edge-pipe-stripping/split steps
// exactly so the two decompose a line into the same cell boundaries in the
// same order -- lineStartByte is line's own absolute start offset, added
// to every returned span so callers get real buffer offsets back. Used by
// each format's own AtPoint layer (Org.cpp, Markdown.cpp) to find which
// cell point currently sits in before realigning -- kept separate from
// SplitRow rather than folded into it, since only that point-tracking path
// needs byte positions at all.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> CellByteSpans(std::string_view line,
                                                                             std::size_t      lineStartByte);

// Scans outward from pointLine (0-indexed, matching every other line-index
// convention in this codebase) while each line, after stripping leading
// whitespace, starts with `|` -- returns the contiguous block's
// [startLine, endLine) line range, or nullopt if pointLine itself doesn't
// qualify (out of range, or doesn't start with `|`) . Format-agnostic: what
// counts as "part of a table" is identical text in both Org and GFM: only
// what a *separator-shaped* row within that block means differs, which is
// each format's own concern, not this function's.
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> FindTableBlockLines(std::string_view bufferText,
                                                                                     std::size_t      pointLine);

} // namespace ned::editor::table

#endif // NED_EDITOR_TABLE_H
