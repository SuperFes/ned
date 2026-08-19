//
// A one-row tab bar listing every open buffer (tab-bar follow-up): the
// active buffer's tab is drawn with a visually distinct brush, clicking a
// tab switches to it (ActiveBuffer::Set), and the mouse wheel scrolls the
// row horizontally when the tabs overflow the available width -- a `‹`/`›`
// indicator (tab-close follow-up) appears at the corresponding edge whenever
// there's more scrolled-past content in that direction, so the overflow is
// visible rather than silently hidden. A visual complement to
// switch-to-buffer, not a replacement for it.
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

    std::function<void(text::Buffer&)> onCloseRequest_;
};

} // namespace ned::ui

#endif // NED_UI_TABBAR_H
