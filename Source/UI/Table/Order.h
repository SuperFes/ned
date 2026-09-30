//
// The lines a TableView shows, in order: group headers and the rows under
// them, sorted within each group and minus collapsed groups' rows. Pure, so
// ordering is testable without a widget.
//

#ifndef NED_UI_TABLE_ORDER_H
#define NED_UI_TABLE_ORDER_H

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Model.h"

namespace ned::ui::table {

struct Sort {
    std::string columnId;
    bool        descending = false;

    bool operator==(const Sort&) const = default;
};

struct Line {
    enum class Kind { GroupHeader,
                      Row };
    Kind        kind  = Kind::Row;
    std::size_t group = 0; // into Model::groups
    std::size_t row   = 0; // into that group's rows; unused for a header

    bool operator==(const Line&) const = default;
};

// Negative, zero or positive, ascending. A blank cell (no text, no number)
// sorts after everything else in either direction, so it's handled by
// BuildLines rather than here. Numbers compare as numbers; text compares
// case-insensitively with digit runs taken as numbers, so NED-9 comes
// before NED-10.
[[nodiscard]] int CompareCells(const Cell& a, const Cell& b);

// Rows keep the model's order unless sorted; ties keep it too. A sort
// naming a column the model doesn't have, or an unsortable one, is ignored.
[[nodiscard]] std::vector<Line> BuildLines(const Model& model, const std::optional<Sort>& sort,
                                           const std::set<std::string>& collapsedGroups);

} // namespace ned::ui::table

#endif // NED_UI_TABLE_ORDER_H
