#include "TranscriptScroll.h"

#include <algorithm>

namespace ned::ui::acppanel {

namespace {

    int MaxTop(int totalRows, int viewportRows) {
        return std::max(0, totalRows - std::max(0, viewportRows));
    }

} // namespace

int TranscriptScroll::FirstVisibleRow(int totalRows, int viewportRows) {
    const int maxTop = MaxTop(totalRows, viewportRows);
    if (!following_ && top_ >= maxTop) {
        following_ = true;
    }
    return following_ ? maxTop : std::max(0, top_);
}

void TranscriptScroll::ScrollBy(int deltaRows, int totalRows, int viewportRows) {
    ScrollToRow(FirstVisibleRow(totalRows, viewportRows) + deltaRows, totalRows, viewportRows);
}

void TranscriptScroll::ScrollToRow(int row, int totalRows, int viewportRows) {
    const int maxTop = MaxTop(totalRows, viewportRows);
    top_             = std::clamp(row, 0, maxTop);
    following_       = top_ >= maxTop;
}

void TranscriptScroll::ScrollToTop() {
    top_       = 0;
    following_ = false;
}

void TranscriptScroll::FollowTail() {
    following_ = true;
}

} // namespace ned::ui::acppanel
