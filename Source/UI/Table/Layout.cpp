#include "Layout.h"

#include <algorithm>

namespace ned::ui::table {

namespace {

    int FitWidth(const Column& column, int content) {
        return std::clamp(content, column.minWidth, std::max(column.minWidth, column.maxWidth));
    }

    int MinimumWidth(const Column& column, int content) {
        return column.width == Column::Width::Flex ? column.minWidth : FitWidth(column, content);
    }

} // namespace

std::vector<ColumnSlot> LayoutColumns(std::span<const Column> columns, std::span<const int> contentWidths, int available,
                                      int gap) {
    const auto content = [&](std::size_t i) { return i < contentWidths.size() ? contentWidths[i] : 0; };

    std::vector<std::size_t> shown;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        shown.push_back(i);
    }
    const auto required = [&] {
        int total = shown.empty() ? 0 : gap * static_cast<int>(shown.size() - 1);
        for (const std::size_t i : shown) {
            total += MinimumWidth(columns[i], content(i));
        }
        return total;
    };

    while (required() > available) {
        // Ties drop the rightmost, which is usually the least important.
        auto victim = shown.end();
        for (auto it = shown.begin(); it != shown.end(); ++it) {
            if (columns[*it].dropPriority > 0 &&
                (victim == shown.end() || columns[*it].dropPriority >= columns[*victim].dropPriority)) {
                victim = it;
            }
        }
        if (victim == shown.end()) {
            break;
        }
        shown.erase(victim);
    }

    std::vector<int> widths;
    std::size_t      flexCount = 0;
    for (const std::size_t i : shown) {
        widths.push_back(MinimumWidth(columns[i], content(i)));
        flexCount += columns[i].width == Column::Width::Flex ? 1 : 0;
    }

    int spare = available - required();
    if (flexCount > 0) {
        // Positive spare is shared out; negative is taken back, down to one
        // cell per Flex column. Leftmost columns get the odd cell either way.
        const int share = spare / static_cast<int>(flexCount);
        int       extra = spare % static_cast<int>(flexCount);
        for (std::size_t k = 0; k < shown.size(); ++k) {
            if (columns[shown[k]].width != Column::Width::Flex) {
                continue;
            }
            int delta = share;
            if (extra > 0) {
                ++delta;
                --extra;
            }
            else if (extra < 0) {
                --delta;
                ++extra;
            }
            widths[k] = std::max(1, widths[k] + delta);
        }
    }

    std::vector<ColumnSlot> slots;
    int                     x = 0;
    for (std::size_t k = 0; k < shown.size(); ++k) {
        if (x >= available) {
            break;
        }
        slots.push_back(ColumnSlot{.column = shown[k], .x = x, .width = std::min(widths[k], available - x)});
        x += widths[k] + gap;
    }
    return slots;
}

} // namespace ned::ui::table
