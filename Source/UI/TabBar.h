//
// A one-row tab bar listing every open buffer (tab-restyle follow-up: back
// to one row -- the chrome redesign's second underline row read as too
// tall in daily use). Each tab is its own distinct block on a row filled
// with the buffer background, ending in a `▌` half-block cap drawn in the
// tab's own block color (see kTabEndCap in the .cpp) -- the cap's
// half-empty cell doubles as the separation between adjacent tabs, so the
// strip never merges into one chrome-colored bar and needs no separate
// gap column. The
// active buffer's tab is drawn with a visually distinct brush -- and takes
// the accent-tinted chrome background while an editor pane holds the
// keyboard focus (see SetFocusProvider), the top-edge counterpart of the
// focused mode-line gradient. Clicking a tab switches to it
// (ActiveBuffer::Set), and the mouse wheel scrolls the row horizontally
// when the tabs overflow the available width -- a `‹`/`›` indicator
// (tab-close follow-up) appears at the corresponding edge whenever there's
// more scrolled-past content in that direction, so the overflow is visible
// rather than silently hidden. Whenever the active buffer *changes*
// (tab-reveal follow-up), Paint scrolls just far enough to make its tab
// fully visible -- a freshly opened file's tab past the right edge was
// otherwise invisible -- without fighting manual wheel-scrolling in
// between. Dragging a tab left/right reorders it (tab-reorder follow-up,
// via SetOnReorder -- same handler indirection as SetOnCloseRequest). A
// visual complement to switch-to-buffer, not a replacement for it.
//
// Each tab also has a close icon (`×`, tab-close follow-up) -- clicking it
// requests closing that buffer via SetOnCloseRequest's handler rather than
// closing it directly here: TabBar takes no keyboard focus, so it cannot
// itself drive the keyboard y/n confirmation a modified buffer needs before
// being discarded (see BufferView::RequestCloseBuffer, the handler main.cpp
// wires in).
//

#ifndef NED_UI_TABBAR_H
#define NED_UI_TABBAR_H

#include <functional>
#include <vector>

#include "ActiveBuffer.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class TabBar : public Widget {
  public:
    // activeBufferProvider, bufferList, and theme must outlive this TabBar.
    // activeBufferProvider (window-splitting follow-up; was a fixed
    // ActiveBuffer&) is called fresh on every click/paint rather than bound
    // once at construction, since which ActiveBuffer a tab click should
    // retarget changes as keyboard focus moves between panes -- there is no
    // single, permanently-correct ActiveBuffer to hold a direct reference
    // to anymore. main.cpp wires this to WindowManager::FocusedActiveBuffer.
    TabBar(std::function<ActiveBuffer&()> activeBufferProvider, const text::BufferList& bufferList,
           const Theme& theme);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

    // Called with the buffer whose close icon was clicked. Unset (the
    // default) means clicking a close icon is a no-op -- TabBar has no way
    // to safely discard a modified buffer's changes on its own.
    void SetOnCloseRequest(std::function<void(text::Buffer&)> handler);

    // Tab-reorder follow-up: called with a drag-reordered buffer and the
    // tab index it should move to. Unset (the default) means dragging a tab
    // is a no-op -- TabBar holds the BufferList by const reference and
    // cannot reorder it itself; main.cpp wires this to
    // text::BufferList::MoveBufferToIndex, the same "mutation happens
    // outside, through a registered handler" shape SetOnCloseRequest uses.
    void SetOnReorder(std::function<void(text::Buffer&, std::size_t)> handler);

    // Chrome-focus follow-up (retargeted by the tab-restyle follow-up from
    // the removed underline row): when set and returning true, the active
    // tab's block takes the accent-tinted chrome background
    // (modeLineFocusedGradientStart) -- the same "this side has your
    // keyboard" signal ProjectSidebar's accent frame gives when *it* is
    // focused. Unset (the default, every pre-existing construction site
    // and test) means never lit. main.cpp wires this to
    // WindowManager::HasFocusedPane.
    void SetFocusProvider(std::function<bool()> provider);

    // TabBar-context-menu follow-up: called with the right-clicked tab's
    // buffer and the click's absolute screen position -- TabBar only ever
    // signals *which* tab and *where*, the same "mouse-driven widget hands
    // off" shape SetOnCloseRequest/SetOnReorder already establish, since
    // building/showing the actual menu needs main.cpp's own
    // OverlayHost/ListPopup and WindowManager, neither of which TabBar
    // knows about. Unset (the default) means right-click is a no-op.
    void SetOnContextMenuRequest(std::function<void(text::Buffer&, Point)> handler);

  private:
    // One tab's horizontal extent in unscrolled column space (i.e. before
    // subtracting scrollOffset_), plus which buffer it represents. Recomputed
    // fresh by Paint()/OnEvent() rather than cached, same "no persisted
    // layout state" approach BufferView's own gutter/click translation uses.
    struct TabLayout {
        int           startColumn;
        int           endColumn;   // exclusive
        int           closeColumn; // the single column the "×" icon occupies
        text::Buffer* buffer;
    };

    [[nodiscard]] std::vector<TabLayout> ComputeTabLayout() const;

    std::function<ActiveBuffer&()> activeBufferProvider_;
    const text::BufferList&        bufferList_;
    const Theme&                   theme_;

    int scrollOffset_ = 0; // columns of tab content scrolled past on the left

    // Active-tab auto-reveal (tab-reveal follow-up): the buffer whose tab
    // Paint() last revealed. Compared by identity only, never dereferenced
    // (it may have been closed since -- same convention BufferView's
    // topLineValidatedBuffer_ uses), so Paint() adjusts scrollOffset_ to
    // bring the active tab into view exactly once per activation, and a
    // manual wheel-scroll away from it afterwards isn't fought.
    const text::Buffer* lastRevealedActive_ = nullptr;

    // Tab-reorder follow-up: the buffer whose tab a left-press landed on,
    // tracked until the matching release so Moved events reorder it (see
    // SetOnReorder). Never dereferenced without onReorder_ set.
    text::Buffer* dragBuffer_ = nullptr;

    std::function<void(text::Buffer&)>              onCloseRequest_;
    std::function<void(text::Buffer&, std::size_t)> onReorder_;     // see SetOnReorder
    std::function<bool()>                           focusProvider_; // see SetFocusProvider
    std::function<void(text::Buffer&, Point)>       onContextMenuRequest_; // see SetOnContextMenuRequest
};

} // namespace ned::ui

#endif // NED_UI_TABBAR_H
