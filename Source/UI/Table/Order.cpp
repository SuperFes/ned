#include "Order.h"

#include <algorithm>
#include <numeric>
#include <string_view>

namespace ned::ui::table {

namespace {

    bool IsDigit(char c) {
        return c >= '0' && c <= '9';
    }

    char Lower(char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

    int NaturalCompare(std::string_view a, std::string_view b) {
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < a.size() && j < b.size()) {
            if (IsDigit(a[i]) && IsDigit(b[j])) {
                const std::size_t aStart = i;
                const std::size_t bStart = j;
                while (i < a.size() && IsDigit(a[i])) {
                    ++i;
                }
                while (j < b.size() && IsDigit(b[j])) {
                    ++j;
                }
                std::string_view aRun = a.substr(aStart, i - aStart);
                std::string_view bRun = b.substr(bStart, j - bStart);
                aRun.remove_prefix(std::min(aRun.find_first_not_of('0'), aRun.size()));
                bRun.remove_prefix(std::min(bRun.find_first_not_of('0'), bRun.size()));
                if (aRun.size() != bRun.size()) {
                    return aRun.size() < bRun.size() ? -1 : 1;
                }
                if (const int order = aRun.compare(bRun); order != 0) {
                    return order;
                }
                continue;
            }
            const char ca = Lower(a[i]);
            const char cb = Lower(b[j]);
            if (ca != cb) {
                return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb) ? -1 : 1;
            }
            ++i;
            ++j;
        }
        if (i < a.size()) {
            return 1;
        }
        return j < b.size() ? -1 : 0;
    }

    bool IsBlank(const Cell& cell) {
        return !cell.sortNumber && cell.text.empty();
    }

    const Cell& CellAt(const Row& row, std::size_t column) {
        static const Cell kEmpty;
        return column < row.cells.size() ? row.cells[column] : kEmpty;
    }

} // namespace

int CompareCells(const Cell& a, const Cell& b) {
    if (a.sortNumber && b.sortNumber) {
        return *a.sortNumber < *b.sortNumber ? -1 : *a.sortNumber > *b.sortNumber ? 1 : 0;
    }
    if (a.sortNumber || b.sortNumber) {
        return a.sortNumber ? -1 : 1;
    }
    return NaturalCompare(a.text, b.text);
}

std::vector<Line> BuildLines(const Model& model, const std::optional<Sort>& sort,
                             const std::set<std::string>& collapsedGroups) {
    std::optional<std::size_t> sortColumn;
    if (sort) {
        const auto found = std::ranges::find(model.columns, sort->columnId, &Column::id);
        if (found != model.columns.end() && found->sortable) {
            sortColumn = static_cast<std::size_t>(found - model.columns.begin());
        }
    }

    std::vector<Line> lines;
    for (std::size_t g = 0; g < model.groups.size(); ++g) {
        const Group& group = model.groups[g];
        if (!group.id.empty()) {
            lines.push_back(Line{.kind = Line::Kind::GroupHeader, .group = g});
            if (collapsedGroups.contains(group.id)) {
                continue;
            }
        }
        std::vector<std::size_t> order(group.rows.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        if (sortColumn) {
            const bool descending = sort->descending;
            std::ranges::stable_sort(order, [&](std::size_t left, std::size_t right) {
                const Cell& a = CellAt(group.rows[left], *sortColumn);
                const Cell& b = CellAt(group.rows[right], *sortColumn);
                if (IsBlank(a) || IsBlank(b)) {
                    return !IsBlank(a) && IsBlank(b);
                }
                const int order = CompareCells(a, b);
                return descending ? order > 0 : order < 0;
            });
        }
        for (const std::size_t r : order) {
            lines.push_back(Line{.kind = Line::Kind::Row, .group = g, .row = r});
        }
    }
    return lines;
}

} // namespace ned::ui::table
