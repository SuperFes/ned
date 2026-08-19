#include "ScrollBar.h"

#include <algorithm>

namespace ned::ui {

namespace {

    constexpr char kThumbChar = ' '; // rendered with an inverted-looking solid brush, see Paint()
    constexpr char kTrackChar = ' ';

} // namespace

ScrollBar::ScrollBar(const Brush& brush) : brush_(brush) {
}

void ScrollBar::SetOnScroll(std::function<void(int)> onScroll) {
    onScroll_ = std::move(onScroll);
}

void ScrollBar::Paint(Canvas c) {
    const int height = c.size().height;
    if (height <= 0) {
        return;
    }

    const int total = std::max(scrollable_length, 1);
    // Thumb size proportional to how much of the content one screen shows,
    // never smaller than 1 row so it's always visible/grabbable even for a
    // huge document.
    const int thumbRows =
        std::clamp(static_cast<int>((static_cast<long long>(item_visual_length) * height) / total), 1, height);
    const int maxThumbStart = height - thumbRows;
    const int thumbStart =
        (total > item_visual_length)
            ? std::clamp(static_cast<int>((static_cast<long long>(position) * maxThumbStart) /
                                          std::max(total - item_visual_length, 1)),
                         0, maxThumbStart)
            : 0;

    for (int y = 0; y < height; ++y) {
        const bool onThumb = y >= thumbStart && y < thumbStart + thumbRows;
        Cell&      cell    = c[{.x = 0, .y = y}];
        cell.character     = std::string(1, onThumb ? kThumbChar : kTrackChar);
        brush_.ApplyTo(cell);
        cell.inverted = onThumb; // solid block look for the thumb, distinct from the plain track
    }
}

int ScrollBar::PositionForRow(int row) const {
    const int height = size().height;
    if (height <= 0) {
        return 0;
    }
    const int total = std::max(scrollable_length, 1);
    if (total <= item_visual_length) {
        return 0;
    }
    const int maxPosition = total - item_visual_length;
    const int clampedRow  = std::clamp(row, 0, height - 1);
    return std::clamp(static_cast<int>((static_cast<long long>(clampedRow) * total) / height), 0, maxPosition);
}

bool ScrollBar::OnEvent(const Event& event) {
    if (const auto mouse = LocalMouseEvent(event)) {
        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            dragging_ = true;
            if (onScroll_) {
                onScroll_(PositionForRow(mouse->at.y));
            }
            return true;
        }
        if (mouse->motion == MouseEvent::Motion::Moved && dragging_) {
            if (onScroll_) {
                onScroll_(PositionForRow(mouse->at.y));
            }
            return true;
        }
    }
    if (event.is_mouse() && event.mouse().motion == MouseEvent::Motion::Released && dragging_) {
        dragging_ = false;
        return true;
    }
    return false;
}

} // namespace ned::ui
