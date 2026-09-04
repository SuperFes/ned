//
// Debugging wishlist follow-up: memory-as-image viewer (gf's RGBA bitmap
// Data tab) -- dap-show-memory-image-at-point's display half. Same
// TreeView-precedent OverlayHost shape as the pointer-graph/call-hierarchy
// viewers (a pure renderer over repeated SetModel() calls, no session
// state of its own; BufferView owns the DapManager::MemoryBlock fetch and
// WindowManager routes Cancel to whichever pane's session is live), but a
// read-only image instead of a navigable list -- no selection, no
// activate/expand, just Escape/C-g/any-other-key to dismiss.
//
// Renders via Editor/MemoryImage.h's pure grayscale-byte/square-layout
// helpers using the half-block-glyph doubling trick (U+2580 UPPER HALF
// BLOCK painted with distinct foreground/background colors packs two
// vertically-stacked pixel rows into one terminal cell row) rather than
// Minimap.h's real ncvisual/NCBLIT_PIXEL path -- see MemoryImage.h's own
// header comment for why.
//

#ifndef NED_UI_MEMORYIMAGEVIEW_H
#define NED_UI_MEMORYIMAGEVIEW_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

struct MemoryImageModel {
    std::string               title; // e.g. "Memory image: 0x7ffee... (128 bytes)"
    std::vector<std::uint8_t> bytes;
};

class MemoryImageView : public Widget {
  public:
    explicit MemoryImageView(const Theme& theme);

    // TreeView::SetModel's own contract -- replaces displayed content, does
    // not show/hide the widget (OverlayHost::Show/Hide is the caller's job).
    void SetModel(MemoryImageModel model);

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

    // Escape, C-g, or any other key (this is a read-only display, there's
    // nothing else a keystroke could mean) while focused -- TreeView's own
    // IsQuit-or-consume shape, just widened to "every key" since there's no
    // navigation to preserve.
    void SetOnCancel(std::function<void()> onCancel);

  private:
    const Theme&          theme_;
    MemoryImageModel      model_;
    std::function<void()> onCancel_;
};

} // namespace ned::ui

#endif // NED_UI_MEMORYIMAGEVIEW_H
