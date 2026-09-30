//
// Column geometry for a TableView row: which columns fit the width at all,
// and where each one starts. Pure, so the narrow-dock cases are testable
// without a Canvas.
//

#ifndef NED_UI_TABLE_LAYOUT_H
#define NED_UI_TABLE_LAYOUT_H

#include <cstddef>
#include <span>
#include <vector>

#include "Model.h"

namespace ned::ui::table {

struct ColumnSlot {
    std::size_t column = 0; // into Model::columns
    int         x      = 0; // from the start of the row
    int         width  = 0;

    bool operator==(const ColumnSlot&) const = default;
};

// The columns shown in `available` cells, left to right, `gap` blank cells
// apart. contentWidths[i] is the widest of column i's header and cells.
// Columns with a dropPriority are dropped, highest first, until the rest fit
// at their minimum widths; if even that overflows, Flex columns shrink below
// their minimum and whatever still doesn't fit is cut at the right edge.
[[nodiscard]] std::vector<ColumnSlot> LayoutColumns(std::span<const Column> columns, std::span<const int> contentWidths,
                                                    int available, int gap = 1);

} // namespace ned::ui::table

#endif // NED_UI_TABLE_LAYOUT_H
