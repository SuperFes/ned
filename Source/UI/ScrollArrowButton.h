//
// A single clickable glyph, one row tall -- the up/down arrow caps flanking
// the scroll bar (scroll-bar follow-up: "we're not Emacs, we can look
// better").
//

#ifndef NED_UI_SCROLLARROWBUTTON_H
#define NED_UI_SCROLLARROWBUTTON_H

#include <chrono>
#include <functional>

#include <ftxui/component/animation.hpp>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class ScrollArrowButton : public Widget {
  public:
    // brush/disabledBrush must outlive this widget (references into Theme,
    // the usual convention). onClick starts unset -- wired in after
    // construction via SetOnClick, same "connect after the widget tree
    // exists" pattern main.cpp already uses elsewhere. Starts enabled;
    // SetEnabled(false) (driven by BufferView::SetScrollArrows -- there's
    // nothing above/below to scroll to, e.g. the whole buffer already fits
    // on screen) switches to disabledBrush and ignores clicks.
    //
    // Auto-repeats while held, like a key held down: a press fires onClick_
    // once immediately, then keeps firing it on a fixed interval via
    // FTXUI's own animation::RequestAnimationFrame/OnAnimation mechanism
    // (TermOx -> FTXUI migration -- was a dedicated ox::Timer; FTXUI has no
    // free-running background-thread timer exposed at this level, but does
    // have this render-loop-tied animation-step hook, which serves the same
    // "fire a callback repeatedly while active" role). Stops on any mouse
    // Released event, not specifically one landing on this widget's own
    // bounds -- unlike the old mouse_leave-specific workaround this
    // replaces (needed because TermOx's per-widget dispatch was itself
    // position-filtered, so a press-then-drag-off-the-button gesture would
    // never otherwise deliver a release here), FTXUI delivers every mouse
    // event to every leaf widget regardless of position (see Widget.h's own
    // header comment), so a plain "was anything released" check already
    // covers the drag-off case with no separate leave concept needed.
    ScrollArrowButton(char32_t symbol, const Brush& brush, const Brush& disabledBrush);

    void SetOnClick(std::function<void()> onClick);
    void SetEnabled(bool enabled);

    // Whether the repeat is currently active (held down). Mainly for tests
    // -- avoids asserting on real elapsed time/tick counts, which would be
    // flaky.
    [[nodiscard]] bool IsRepeating() const;

    void Paint(Canvas c) override;
    bool OnEvent(ftxui::Event event) override;
    void OnAnimation(ftxui::animation::Params& params) override;

  private:
    void StartRepeating();
    void StopRepeating();

    char32_t                        symbol_;
    const Brush&                    brush_;
    const Brush&                    disabledBrush_;
    bool                             enabled_    = true;
    bool                             repeating_  = false;
    ftxui::animation::Duration       accumulated_{0};
    std::function<void()>           onClick_;
};

} // namespace ned::ui

#endif // NED_UI_SCROLLARROWBUTTON_H
