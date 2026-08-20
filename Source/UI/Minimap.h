//
// Minimap widget follow-up. Replaces ScrollBar for a pane (WindowManager.cpp
// toggles between the two via each other's own `active` flag, per the
// user's own explicit choice during planning): a narrow (Editor/
// MinimapSettings.h-configurable, default 5 columns), braille-glyph, real-
// syntax-colored zoomed-out preview of the whole buffer that also acts as
// the scrollbar -- click/drag exactly mirrors ScrollBar's own
// PositionForRow/dragging_ shape (Source/UI/ScrollBar.cpp), and exposes the
// same public scrollable_length/position/item_visual_length/SetOnScroll
// surface so BufferView's existing per-Paint() sync code has a directly
// analogous SetMinimap to add.
//
// Real per-pixel rendering (Notcurses' NCBLIT_PIXEL, confirmed working via
// Tools/NotcursesPixelProbe.cpp) is a deliberate v1 scope cut -- this
// codebase's whole Source/UI/ render pipeline funnels through one shared
// Screen/Cell grid (Widget.h), with no per-widget ncplane access at all;
// real pixel blitting needs a new Screen/Flush-level bridging seam that
// doesn't exist yet. Braille instead: works identically in every terminal,
// no new Notcurses integration risk.
//

#ifndef NED_UI_MINIMAP_H
#define NED_UI_MINIMAP_H

#include <cstdint>
#include <functional>
#include <vector>

#include "ActiveBuffer.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class Minimap : public Widget {
  public:
    // activeBuffer, mode, and theme must outlive this Minimap -- same
    // convention ModeLine's own constructor already establishes (both need
    // exactly buffer+mode+theme and nothing else).
    Minimap(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme);

    // Synced fresh every frame by BufferView (mirrors ScrollBar's own
    // public fields exactly, including semantics): position ranges over
    // [0, scrollable_length - 1], item_visual_length is how many of those
    // units one visible row represents.
    int scrollable_length  = 1;
    int position           = 0;
    int item_visual_length = 1;

    // Called with a new position (already clamped to
    // [0, scrollable_length - 1]) on a click or drag -- wired to
    // BufferView::SetTopLine by BufferView::SetMinimap, the same
    // "connect after construction" pattern ScrollBar's own SetOnScroll
    // already establishes.
    void SetOnScroll(std::function<void(int)> onScroll);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

  private:
    // One rendered braille character cell: glyph starts at U+2800 (blank)
    // and accumulates dot bits directly via |= (every braille codepoint is
    // U+2800 plus a bitmask, so this stays a valid glyph at every step);
    // foreground is the first non-Default SyntaxClass color found in this
    // cell's mapped buffer region, in scan order -- unset (Color::Default)
    // if the whole region was blank/whitespace.
    struct MinimapCell {
        char32_t glyph = U'⠀';
        Color    foreground;
    };

    // (Re)builds grid_ from the active buffer's content + mode_.highlight,
    // gated on (buffer identity, ContentGeneration(), size().height,
    // MinimapWidth(), MinimapCharsPerDot()) all staying unchanged since the
    // last call -- same generation-gated-cache shape
    // BufferView::highlightCacheBuffer_ already established, computed
    // independently here rather than through any new BufferView API (see
    // this class's own header comment).
    void EnsureCache() const;

    // Row -> position, inverse of the vertical line-density mapping
    // EnsureCache() uses -- shared so a click always lands exactly where
    // it visually appears to. Identical formula to ScrollBar::PositionForRow,
    // just against this widget's own scrollable_length/item_visual_length.
    [[nodiscard]] int PositionForRow(int row) const;

    const ActiveBuffer& activeBuffer_;
    const editor::Mode&  mode_;
    const Theme&          theme_;

    std::function<void(int)> onScroll_;
    bool                      dragging_ = false;

    mutable text::Buffer*            cacheBuffer_            = nullptr;
    mutable std::size_t              cacheContentGeneration_ = 0;
    mutable int                      cacheHeight_            = -1;
    mutable int                      cacheWidth_             = -1;
    mutable int                      cacheCharsPerDot_       = -1;
    mutable std::vector<MinimapCell> grid_;                         // cacheWidth_ x cacheHeight_, row-major
};

} // namespace ned::ui

#endif // NED_UI_MINIMAP_H
