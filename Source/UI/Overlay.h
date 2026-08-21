//
// Terminal-panel follow-up: the floating/overlay widget layer this codebase
// never had (see BufferView.h's M-x candidate rendering and EchoArea.h's
// fuzzy-list sentinels for the workarounds its absence forced, and
// ROADMAP.md's LLM-panel note calling the gap out directly). Deliberately
// tiny and entirely outside Layout.h's Container system: a Container child
// occupies exclusive, non-overlapping space, while an overlay floats *over*
// the composed tree -- possible precisely because every widget paints into
// the one shared Screen and z-order is a plain painter's algorithm (last
// writer to a cell wins), so "on top" just means "painted after head."
//
// The composition root owns one OverlayHost and gives it three hooks, all
// no-ops while nothing is shown:
//  - render:   overlays.Paint(screenBuffer) between head.Paint and Flush
//  - onResize: overlays.Reflow(size) after head.SetBox_
//  - onEvent:  if (!overlays.OnMouseEvent(event)) head.OnEvent(event)
// Keyboard needs no hook at all -- dispatch already goes only to
// FocusedWidget() (Widget.h's flat registry), so a focused overlay receives
// every keystroke and everything else receives none, with no modal-stack
// concept needed anywhere.
//
// Mouse interception exists because Container::OnEvent broadcasts every
// mouse event to every active leaf unconditionally: without it, a click on
// an overlay would also land in whatever it covers. Interception is
// containment-scoped -- only while an overlay is visible, and only inside
// its Box -- so the widgets that actively depend on receiving mouse events
// outside their own bounds (ScrollArrowButton's release check,
// ProjectSidebar's resize-drag cooperation; see Widget.h's header comment)
// keep that behavior whenever the event isn't over an overlay.
//
// Placement is a function, not a stored Box: the drawer-style terminal
// panel closes over nothing and derives from the terminal size alone, while
// a future anchored popup (completion popover at point, M-x dropdown) can
// close over an anchor its consumer updates -- both re-derived on every
// Reflow/Show, the same recompute-fresh convention every per-frame sync in
// this codebase already follows.
//

#ifndef NED_UI_OVERLAY_H
#define NED_UI_OVERLAY_H

#include <functional>
#include <vector>

#include "Widget.h"

namespace ned::ui {

class OverlayHost {
  public:
    // Absolute screen-space Box for the overlay, derived from the current
    // terminal size.
    using PlacementFn = std::function<Box(Size terminalSize)>;

    // Registers widget, initially hidden (widget.active = false). The
    // widget must outlive this host; the host never owns it, mirroring how
    // main.cpp's composition owns every other widget directly.
    void Add(Widget& widget, PlacementFn placement);

    // Makes widget visible, re-boxes it from the last known terminal size,
    // and raises it above any other visible overlay.
    void Show(Widget& widget);

    // Hides widget. If it currently holds keyboard focus, its registered
    // focus-return callback runs (the SetOnFocusReturn pattern) -- without
    // that, nothing would hold keys afterward.
    void Hide(Widget& widget);

    void SetFocusReturn(Widget& widget, std::function<void()> onFocusReturn);

    [[nodiscard]] bool IsVisible(const Widget& widget) const;

    // Caches the terminal size and re-boxes every visible overlay --
    // called from onResize; nothing else ever boxes an overlay, since it
    // isn't a Container child.
    void Reflow(Size terminalSize);

    // Paints every visible overlay, bottom to top, over whatever the main
    // tree already painted into screen this frame.
    void Paint(Screen& screen) const;

    // Topmost-first containment test: a mouse event inside a visible
    // overlay's Box is delivered to that overlay alone and consumed (true).
    // Anything else -- keyboard, or mouse outside every overlay -- is left
    // for the caller's normal dispatch (false).
    bool OnMouseEvent(const Event& event);

  private:
    struct Entry {
        Widget*               widget = nullptr;
        PlacementFn           placement;
        std::function<void()> onFocusReturn;
    };

    [[nodiscard]] Entry*       FindEntry(const Widget& widget);
    [[nodiscard]] const Entry* FindEntry(const Widget& widget) const;

    std::vector<Entry> entries_; // paint order -- back() is topmost
    Size               lastSize_{};
};

} // namespace ned::ui

#endif // NED_UI_OVERLAY_H
