//
// Markdown (GFM) table support -- this codebase's first Markdown-specific
// Editor-layer file, deliberately scoped to *only* table parsing + column
// alignment, not a general Markdown-editing grab-bag. Built on
// Source/Editor/Table.h's shared, format-agnostic toolkit -- the same one
// Source/Editor/Org.h's own table support uses -- since the column-width/
// alignment engine is identical between Org and GFM tables even though
// their exact separator-row syntax isn't: GFM requires a *mandatory*
// second "delimiter" row (`:---`/`:---:`/`---:`/`---` per column, `-` at
// intersections), unlike Org's optional, alignment-free `-+-` hrule.
//

#ifndef NED_EDITOR_MARKDOWN_H
#define NED_EDITOR_MARKDOWN_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Table.h"
#include "Text/Buffer.h"

namespace ned::editor::markdown {

// rows[0] is the header row; rows[1..] are data rows -- the mandatory
// delimiter row itself is never stored here, only its per-column meaning
// (columnAlignments), since it carries no real content of its own.
struct Table {
    std::vector<std::vector<std::string>> rows;
    std::vector<table::Alignment>         columnAlignments;
    std::size_t                           startLine;
    std::size_t                           endLine; // exclusive
};

// A contiguous |-prefixed block (Source/Editor/Table.h's FindTableBlockLines)
// only counts as a real GFM table if it has at least a header and a
// delimiter row, and that second row is a *valid* delimiter row (every
// cell is `:`-optional dashes, e.g. ":---", "---:", ":---:", "---") --
// returns nullopt otherwise, even though FindTableBlockLines itself would
// still have matched the block (a block of plain `|`-prefixed lines with
// no valid delimiter row isn't a real GFM table, just text that happens to
// contain pipes).
[[nodiscard]] std::optional<Table> FindTableAtPoint(const text::Buffer& buffer);

// Realigns every column (per the delimiter row's own alignment markers)
// to its content's own width, then moves point to the next cell in
// row-major order across the header and data rows -- same interactive
// shape as Org.h's AlignOrgTableAtPoint. Tabbing past the table's last
// cell appends a fresh empty data row and lands on its first cell (real
// Org's/real GFM editors' own TAB behavior) rather than wrapping back to
// the header. Returns false (buffer untouched) if point isn't inside a
// valid table.
[[nodiscard]] bool AlignTableAtPoint(text::Buffer& buffer);

// The S-TAB mirror of AlignTableAtPoint: realigns, then moves point to the
// previous cell in row-major order, wrapping from the table's first cell
// (the header's own first cell) back to its last -- no auto-append here,
// same as Org.h's MoveToPreviousOrgTableCellAtPoint (there's no "before
// the table" row to create). Returns false if point isn't inside a table.
[[nodiscard]] bool MoveToPreviousTableCellAtPoint(text::Buffer& buffer);

// The row-editing ops, each realigning the whole table as a side effect
// the same way AlignTableAtPoint does (mirrors Org.h's own table-editing
// surface) -- with one structural difference from Org: GFM's header row
// is mandatory, so every op here refuses (returns false) when point sits
// on the header or delimiter line rather than treating it like any other
// row. All other failure returns false leaving buffer untouched: not
// inside a table, or (move ops) already at the data rows' own edge.
//
// InsertTableRowAtPoint inserts an empty data row ABOVE the current data
// row (or, from the header/delimiter line, as the new first data row),
// leaving point in the new row at its own current column.
// KillTableRowAtPoint removes the current data row outright -- killing
// the table's only data row is allowed (the table degrades to just a
// header + delimiter, still valid GFM, unlike Org's whole-block removal).
[[nodiscard]] bool InsertTableRowAtPoint(text::Buffer& buffer);
[[nodiscard]] bool KillTableRowAtPoint(text::Buffer& buffer);
[[nodiscard]] bool MoveTableRowUpAtPoint(text::Buffer& buffer);
[[nodiscard]] bool MoveTableRowDownAtPoint(text::Buffer& buffer);

// The column-editing ops, same realign-as-a-side-effect/false-off-a-table/
// false-at-the-edge contract as the row ops above. Ragged rows (including
// the header) are padded with empty cells to the table's full column
// count first, matching Org's own column ops. Unlike Org, a column here
// carries alignment state (the delimiter row's marker) that travels with
// it on every insert/delete/move.
//
// InsertTableColumnAtPoint inserts an empty, Default-aligned column to
// the RIGHT of the current one, leaving point in the new column.
// DeleteTableColumnAtPoint refuses (returns false) to delete the table's
// only column -- a zero-column table has no representation in GFM syntax.
[[nodiscard]] bool InsertTableColumnAtPoint(text::Buffer& buffer);
[[nodiscard]] bool DeleteTableColumnAtPoint(text::Buffer& buffer);
[[nodiscard]] bool MoveTableColumnLeftAtPoint(text::Buffer& buffer);
[[nodiscard]] bool MoveTableColumnRightAtPoint(text::Buffer& buffer);

} // namespace ned::editor::markdown

#endif // NED_EDITOR_MARKDOWN_H
