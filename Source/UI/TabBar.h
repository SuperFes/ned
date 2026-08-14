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
// closing it directly here: TabBar is FocusPolicy::None and never receives
// key events, so it cannot itself drive the keyboard y/n confirmation a
// modified buffer needs before being discarded (see BufferView::
// RequestCloseBuffer, the handler main.cpp wires in).
//

#ifndef NED_UI_TABBAR_H
#define NED_UI_TABBAR_H

#include <functional>
#include <vector>

#include <ox/ox.hpp>

#include "ActiveBuffer.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Theme.h"

namespace ned::ui {

class TabBar : public ox::Widget {
  public:
    // activeBuffer, bufferList, and theme must outlive this TabBar.
    TabBar(ActiveBuffer& activeBuffer, const text::BufferList& bufferList, const Theme& theme);

    void paint(ox::Canvas c) override;
    void mouse_press(ox::Mouse mouse) override;
    void mouse_wheel(ox::Mouse mouse) override;

    // Called with the buffer whose close icon was clicked. Unset (the
    // default) means clicking a close icon is a no-op -- TabBar has no way
    // to safely discard a modified buffer's changes on its own.
    void SetOnCloseRequest(std::function<void(text::Buffer&)> handler);

  private:
    // One tab's horizontal extent in unscrolled column space (i.e. before
    // subtracting scrollOffset_), plus which buffer it represents. Recomputed
    // fresh by paint()/mouse_press()/mouse_wheel() rather than cached, same
    // "no persisted layout state" approach BufferView's own gutter/click
    // translation uses.
    struct TabLayout {
        int           startColumn;
        int           endColumn;   // exclusive
        int           closeColumn; // the single column the "×" icon occupies
        text::Buffer* buffer;
    };

    [[nodiscard]] std::vector<TabLayout> ComputeTabLayout() const;

    ActiveBuffer&           activeBuffer_;
    const text::BufferList& bufferList_;
    const Theme&            theme_;

    int scrollOffset_ = 0; // columns of tab content scrolled past on the left

    std::function<void(text::Buffer&)> onCloseRequest_;
};

} // namespace ned::ui

#endif // NED_UI_TABBAR_H
