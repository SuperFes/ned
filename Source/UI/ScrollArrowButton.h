//
// A single clickable glyph, one row tall -- the up/down arrow caps flanking
// the scroll bar (scroll-bar follow-up: "we're not Emacs, we can look
// better").
//

#ifndef NED_UI_SCROLLARROWBUTTON_H
#define NED_UI_SCROLLARROWBUTTON_H

#include <atomic>
#include <functional>
#include <thread>

#include "EventLoop.h"
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
    // once immediately, then keeps firing it on a fixed interval. Notcurses'
    // own EventLoop (Widget.h/EventLoop.h) has no per-frame tick at all (it
    // only wakes for real input or Post()ed work, never on a free-running
    // clock), so this uses a real background std::jthread that sleeps the
    // repeat interval and Post()s onClick_ back onto the loop thread each
    // time, the same "own background thread, marshal back via Post" shape
    // BufferView's scratch-auto-save timer already uses (see BufferView.h's
    // own StartAutoSaveTimer comment).
    // Requires SetEventLoop to have been called first (main.cpp does this
    // right after construction, same "connect after the widget tree
    // exists" pattern SetOnClick already follows) -- a press is simply
    // inert (fires once, never repeats) if it hasn't been, rather than a
    // hard requirement, so a test-constructed ScrollArrowButton with no
    // real EventLoop still behaves sanely. Stops on any mouse Released
    // event, not specifically one landing on this widget's own bounds --
    // Notcurses delivers every mouse event to every leaf widget regardless
    // of position (see Widget.h's own header comment), so a plain "was
    // anything released" check covers a press-then-drag-off-the-button
    // gesture with no separate leave concept needed.
    ScrollArrowButton(char32_t symbol, const Brush& brush, const Brush& disabledBrush);
    ~ScrollArrowButton() override;

    void SetOnClick(std::function<void()> onClick);
    void SetEnabled(bool enabled);
    void SetEventLoop(EventLoop* loop);

    // Whether the repeat is currently active (held down). Mainly for tests
    // -- avoids asserting on real elapsed time/tick counts, which would be
    // flaky.
    [[nodiscard]] bool IsRepeating() const;

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

  private:
    void StartRepeating();
    void StopRepeating();

    char32_t              symbol_;
    const Brush&          brush_;
    const Brush&          disabledBrush_;
    bool                  enabled_ = true;
    std::atomic<bool>     repeating_{false};
    std::function<void()> onClick_;
    EventLoop*            loop_ = nullptr;
    std::jthread          repeatThread_;
};

} // namespace ned::ui

#endif // NED_UI_SCROLLARROWBUTTON_H
