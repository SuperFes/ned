//
// A single clickable glyph, one row tall -- the up/down arrow caps flanking
// the scroll bar (scroll-bar follow-up: "we're not Emacs, we can look
// better"). Deliberately a thin sibling widget rather than built into
// ox::ScrollBar itself, since that's a vendored widget this project doesn't
// patch.
//

#ifndef NED_UI_SCROLLARROWBUTTON_H
#define NED_UI_SCROLLARROWBUTTON_H

#include <functional>

#include <ox/ox.hpp>

namespace ned::ui {

class ScrollArrowButton : public ox::Widget {
  public:
    // brush/disabledBrush must outlive this widget (references into Theme,
    // the usual convention). onClick starts unset -- wired in after
    // construction via SetOnClick, same "connect after the widget tree
    // exists" pattern main.cpp already uses for ox::ScrollBar::on_scroll.
    // Starts enabled; SetEnabled(false) (driven by
    // BufferView::SetScrollArrows -- there's nothing above/below to scroll
    // to, e.g. the whole buffer already fits on screen) switches to
    // disabledBrush and ignores clicks.
    //
    // Auto-repeats while held, like a key held down: mouse_press fires
    // onClick_ once immediately, then starts a fixed-interval ox::Timer that
    // fires it again on every tick. mouse_release stops it -- and so does
    // mouse_leave, since TermOx has no mouse-capture concept (every mouse
    // event, including release, is independently position-hit-tested
    // against whatever's under the cursor *at that moment* -- see
    // Application::any_mouse_event), so a press-then-drag-off-the-button
    // gesture would otherwise never deliver this widget a release at all
    // and leave the repeat running forever.
    ScrollArrowButton(char32_t symbol, const ox::Brush& brush, const ox::Brush& disabledBrush);

    void SetOnClick(std::function<void()> onClick);
    void SetEnabled(bool enabled);

    // Whether the repeat timer is currently running (held down). Mainly for
    // tests -- avoids asserting on real elapsed time/tick counts, which
    // would be flaky.
    [[nodiscard]] bool IsRepeating() const;

    void paint(ox::Canvas c) override;
    void mouse_press(ox::Mouse mouse) override;
    void mouse_release(ox::Mouse mouse) override;
    void mouse_leave() override;
    void timer() override;

  private:
    char32_t              symbol_;
    const ox::Brush&      brush_;
    const ox::Brush&      disabledBrush_;
    bool                  enabled_ = true;
    std::function<void()> onClick_;
    ox::Timer             repeatTimer_;
};

} // namespace ned::ui

#endif // NED_UI_SCROLLARROWBUTTON_H
