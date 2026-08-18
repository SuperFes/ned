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
// row-major order across the header and data rows (wrapping from the
// table's last cell back to its first, i.e. the header's own first cell)
// -- same interactive shape as Org.h's AlignOrgTableAtPoint. Auto-inserting
// a new row when tabbing past the last cell of the last row is explicitly
// deferred, matching that same scope cut; see ROADMAP.md. Returns false
// (buffer untouched) if point isn't inside a valid table.
[[nodiscard]] bool AlignTableAtPoint(text::Buffer& buffer);

} // namespace ned::editor::markdown

#endif // NED_EDITOR_MARKDOWN_H
