//
// A vertical scroll bar: a proportional thumb over a track, click-to-jump
// and drag-to-scroll (scroll-bar follow-up). TermOx -> FTXUI migration: this
// is a genuinely new file, not a port -- ox::ScrollBar was a vendored
// TermOx widget with no FTXUI equivalent, so this rebuilds the same feature
// from scratch rather than dropping it (an explicit hard requirement of the
// migration plan).
//

#ifndef NED_UI_SCROLLBAR_H
#define NED_UI_SCROLLBAR_H

#include <functional>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class ScrollBar : public Widget {
  public:
    // brush must outlive this widget (the usual convention).
    explicit ScrollBar(const Brush& brush);

    // Synced fresh every frame by whoever owns the real scroll state
    // (BufferView), the same "recompute, don't cache" convention every
    // other per-frame sync in this codebase already uses -- mirrors
    // ox::ScrollBar's own public scrollable_length/position/
    // item_visual_length fields exactly, including their semantics:
    // position ranges over [0, scrollable_length - 1], item_visual_length
    // is how many of those units one visible row represents.
    int scrollable_length  = 1;
    int position           = 0;
    int item_visual_length = 1;

    // Called with a new position (already clamped to
    // [0, scrollable_length - 1]) on a click or drag -- wired to
    // BufferView::SetTopLine by main.cpp, the same "connect after
    // construction" pattern used throughout this codebase.
    void SetOnScroll(std::function<void(int)> onScroll);

    void Paint(Canvas c) override;
    bool OnEvent(ftxui::Event event) override;

  private:
    // Row -> position, inverse of the mapping Paint() uses to place the
    // thumb -- shared so a click always lands exactly where it visually
    // appears to.
    [[nodiscard]] int PositionForRow(int row) const;

    const Brush&             brush_;
    std::function<void(int)> onScroll_;
    bool                     dragging_ = false;
};

} // namespace ned::ui

#endif // NED_UI_SCROLLBAR_H
