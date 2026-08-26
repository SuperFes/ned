//
// Minimap widget follow-up. Replaces ScrollBar for a pane (WindowManager.cpp
// toggles between the two via each other's own `active` flag, per the
// user's own explicit choice during planning): a narrow (Editor/
// MinimapSettings.h-configurable, default 5 columns), real-syntax-colored
// zoomed-out preview of the whole buffer that also acts as the scrollbar --
// click/drag exactly mirrors ScrollBar's own PositionForRow/dragging_ shape
// (Source/UI/ScrollBar.cpp), and exposes the same public
// scrollable_length/position/item_visual_length/SetOnScroll surface so
// BufferView's existing per-Paint() sync code has a directly analogous
// SetMinimap to add.
//
// Rendering: builds one real RGBA density image (ForEachDensityDot -- one
// dot per non-blank character, syntax-colored, same "1-2 chars per pixel"
// compression the feature always had) and hands it to Notcurses'
// ncvisual_blit, into a plane this widget owns outright (EnsurePlane()) --
// letting Notcurses itself pick and draw the actual glyphs (or genuine
// pixels). This sits *outside* the Screen/Cell grid every other widget
// paints into (Widget.h): Notcurses forbids blitting pixels to the standard
// plane itself, and even the non-pixel blitters need a real ncplane to draw
// real glyph+color combinations Cell's single-codepoint/single-fg/single-bg
// shape can't express (a quadrant/sextant/octant glyph blends two colors
// *within* one cell). EventLoop::CanPixelGraphics() picks the blitter:
// NCBLIT_PIXEL (real per-device-pixel graphics, sixel/Kitty/iTerm2) when the
// terminal genuinely supports it, else NCBLIT_DEFAULT -- Notcurses' own
// best-available-glyph-blitter auto-selection (octant -> sextant ->
// quadrant -> half-block -> plain ASCII), never reimplemented here.
//
// Getting NCBLIT_PIXEL working took an embarrassingly long detour: it
// consistently produced wrong-sized planes and squished/braille-quality
// output, which looked exactly like a string of real notcurses bugs (bad
// CHILDPLANE auto-sizing, NCSCALE_STRETCH not actually stretching, ...).
// The *actual* cause was one project-level build bug, unrelated to any of
// that: notcurses-core's own CMakeLists.txt marks its include directory
// PRIVATE, so nothing in this project's CMakeLists.txt was ever telling our
// own targets where the vendored/FetchContent'd headers live -- every
// #include <notcurses/notcurses.h> in this codebase was silently resolving
// to the *system's* installed notcurses headers instead (API-compatible
// enough to compile clean, zero warnings). ncblitter_e's member order
// differs between that system version and the one actually linked, so
// NCBLIT_PIXEL as this project's own compiled code understood it, and
// NCBLIT_PIXEL as the real, linked library understood it, were two
// different integers -- every blit request was silently asking the real
// library for Braille. Fixed once, at the root, in this project's own
// CMakeLists.txt (an explicit target_include_directories(notcurses-core
// INTERFACE ...) call, see its own comment there) -- not anything specific
// to this file. A prior version of this file hand-rolled a
// single-color-per-cell Unicode braille renderer instead of using
// NCBLIT_DEFAULT -- strictly worse (one color per cell instead of real
// two-color glyph blending) for no benefit, removed.
//
// The plane is created once (EnsurePlane(), sized in cells via
// ncplane_create) and reused/reblitted in place across frames -- torn down
// by ReleasePlane() (called from the destructor, and explicitly by
// BufferView's toggle-minimap handler, since Paint() -- the only other
// place that could notice a deactivation -- never runs once Widget::active
// goes false, per Layout.h's Container skipping inactive widgets outright).
//

#ifndef NED_UI_MINIMAP_H
#define NED_UI_MINIMAP_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ActiveBuffer.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "Theme.h"
#include "Widget.h"

struct ncplane; // <notcurses/notcurses.h> -- see plane_

namespace ned::ui {

class EventLoop;

class Minimap : public Widget {
  public:
    // activeBuffer, mode, and theme must outlive this Minimap -- same
    // convention ModeLine's own constructor already establishes (both need
    // exactly buffer+mode+theme and nothing else).
    Minimap(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme);

    // Releases plane_ if one is still live -- a Minimap destroyed mid-
    // session (pane closed, window split torn down) must not leave a stray
    // bitmap/glyph plane behind in the standard pile.
    ~Minimap() override;

    // Wires this Minimap to the owning EventLoop so Paint() can reach
    // StdPlane()/NotcursesContext() to blit into -- same "unset is a safe
    // no-op, connect after construction" Set* convention every other
    // cross-widget dependency in this codebase follows (SetScrollBar,
    // SetProjectSidebar, ...). nullptr (the default) leaves Paint() unable
    // to draw anything at all -- an unset Minimap is a real, if degenerate,
    // configuration (mirrors every other widget's Set* hooks; not expected
    // in practice, since WindowManager::SetEventLoop wires this at startup).
    void SetEventLoop(EventLoop* eventLoop);

    // Tears down plane_ if present, idempotent. Called from the destructor
    // and from BufferView's toggle-minimap handler the instant
    // Widget::active flips to false -- see this class's own header comment
    // on why nothing else could clean this plane up.
    void ReleasePlane() const;

    // per-buffer-highlight-cache follow-up: erases buffer's entry from
    // this Minimap's own per-buffer highlight-span cache (see
    // highlightCacheByBuffer_'s own doc comment below). Called from
    // WindowManager::ReassignPanesShowing -- the shared close funnel every
    // real buffer close already goes through -- for every pane, not just
    // one currently showing buffer.
    void ClearBufferCache(text::Buffer& buffer);

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
    // Line/column -> density-map walk: at whatever subRows x subCols
    // resolution the caller asks for, calls visit(subRow, subCol, offset,
    // linesInRow) for the first non-blank character found within each
    // charsPerDot-wide column group of each real line within charsPerDot's
    // column budget of its line's start -- at most once per (subRow,
    // subCol) per real line, not once per character (see below for why) --
    // offset is that character's buffer byte offset, for a syntax-class
    // color lookup the caller does itself. charsPerDot is an explicit
    // parameter (rather than reading editor::MinimapCharsPerDot()
    // internally) purely so a test can pass its own value directly --
    // minimap-chars-per-dot-pixel-mode follow-up: both real callers (glyph
    // and real-pixel) now pass the same editor::MinimapCharsPerDot() value
    // uniformly; an earlier version of this class hardcoded 1 for the
    // real-pixel path specifically, which silently defeated the setting for
    // anyone actually running in pixel mode. Vertical compression (many
    // source lines into one subRow) is unavoidable either way once a file
    // has more lines than subRows, so that part of this walk is identical
    // for both callers -- linesInRow (the same value for every visit() call
    // within one subRow: how many real lines got compressed into it) is
    // what lets a caller weight each hit's contribution rather than one
    // line's ink flatly overwriting every other line compressed into the
    // same row (weighted-minimap-density follow-up: a *references*
    // multibuffer's own repeated separator/rule lines used to dominate every
    // row they shared with the real matched-line content, since a rule
    // line's near-full-width ink was whichever line happened to be walked
    // last for that row).
    void ForEachDensityDot(
        int subRows, int subCols, double charsPerDot,
        const std::function<void(int subRow, int subCol, std::size_t offset, std::size_t linesInRow)>& visit) const;

    // (Re)blits plane_ from the active buffer's content, gated on (buffer
    // identity, ContentGeneration(), size().height, MinimapWidth(),
    // MinimapCharsPerDot(), scrollable_length/position/item_visual_length
    // -- the scroll-position band is baked directly into the raster rather
    // than composited afterward, so it's part of the same reblit rather
    // than a separate per-Paint() overlay) all staying unchanged since the
    // last call. A no-op if nothing in that key changed and plane_ is still
    // alive; always repositions the plane via ncplane_move_yx to track this
    // widget's current Box_() either way, since a layout change (sidebar
    // toggle, split) can move this widget without changing its size.
    // Disables itself (planeUnavailable_) after one blit failure, rather
    // than retrying every single frame forever.
    void EnsurePlane() const;

    // Paint()'s real body: calls EnsurePlane() then paints this widget's
    // own Canvas region as a flat background (the real content comes from
    // plane_, composited on top by the terminal itself, not from any Cell
    // this writes).
    void PaintPlane(Canvas c);

    // Row -> position, inverse of the vertical line-density mapping
    // EnsurePlane() uses -- shared so a click always lands exactly where it
    // visually appears to. Identical formula to ScrollBar::PositionForRow,
    // just against this widget's own scrollable_length/item_visual_length.
    [[nodiscard]] int PositionForRow(int row) const;

    const ActiveBuffer& activeBuffer_;
    const editor::Mode&  mode_;
    const Theme&          theme_;

    std::function<void(int)> onScroll_;
    bool                      dragging_ = false;

    EventLoop* eventLoop_ = nullptr; // see SetEventLoop

    // NED_DEBUG_MOUSE-style opt-in diagnostic log (empty means disabled,
    // the common case) -- traces EnsurePlane()'s capability check, computed
    // geometry, and ncvisual_from_rgba/ncvisual_blit outcomes to a file,
    // since a failure in that path is otherwise silent (Paint() just draws
    // background) and unreproducible from outside a real terminal.
    std::string debugLogPath_;
    void        DebugLog(const std::string& line) const;

    // per-buffer-highlight-cache follow-up: the raster/plane cache below
    // (cacheBuffer_ etc.) is keyed on scroll position/viewport size too, so
    // it invalidates on nearly every scroll tick -- but the expensive part
    // of rebuilding it, mode_.highlight(buffer.Text()) (a real tree-sitter
    // parse + query-capture walk on a fresh Mode, unrelated to scroll
    // position at all), doesn't need to be redone that often. This is a
    // narrower cache just for that call's result, keyed by buffer identity
    // like BufferView's own highlightCacheByBuffer_ -- see that member's
    // doc comment (BufferView.h) for the full reasoning, including why
    // modeName is checked alongside content/class generation. Cleared via
    // ClearBufferCache from the same WindowManager close funnel.
    struct HighlightCacheEntry {
        std::size_t                        contentGeneration = 0;
        std::size_t                        classGeneration   = 0;
        std::string                        modeName;
        std::vector<editor::HighlightSpan> spans;
    };
    mutable std::unordered_map<text::Buffer*, HighlightCacheEntry> highlightCacheByBuffer_;

    mutable text::Buffer* cacheBuffer_              = nullptr;
    mutable std::size_t   cacheContentGeneration_   = 0;
    mutable int           cacheHeight_               = -1;
    mutable int           cacheWidth_                = -1;
    mutable double        cacheCharsPerDot_          = -1.0;
    mutable int           cacheScrollableLength_     = -1;
    mutable int           cachePosition_             = -1;
    mutable int           cacheItemVisualLength_     = -1;
    // Only meaningful (and only compared) in real-pixel-graphics mode --
    // NCBLIT_PIXEL's NCSCALE_NONE path needs the source raster built at
    // exactly the terminal's real cell-pixel resolution (see EnsurePlane()'s
    // own comment on why NCSCALE_NONE specifically), so a live font-size
    // change mid-session has to invalidate the cache same as any other key
    // field here.
    mutable int cachePixelCellDimY_ = -1;
    mutable int cachePixelCellDimX_ = -1;
    // theme-preview-bug follow-up: the raster this cache guards has every
    // syntax color baked into real pixels/glyphs -- unlike every other key
    // field above, a theme change is invisible to buffer identity/content
    // generation/dimensions/scroll position, so without this the minimap
    // kept showing whichever theme was active the last time its cache
    // actually rebuilt, confirmed live to persist right through a live
    // select-theme preview session until something *else* invalidated the
    // cache (switching buffers, most reliably). Compared by value
    // (Theme::operator==), not identity -- theme_ is the same reference
    // throughout, mutated in place by the app's themeApplier_.
    mutable Theme cachedTheme_{};

    mutable ncplane* plane_            = nullptr;
    mutable bool     planeUnavailable_ = false;
};

} // namespace ned::ui

#endif // NED_UI_MINIMAP_H
