//
// Debugging wishlist follow-up: memory-as-image viewer (gf's RGBA bitmap
// Data tab) -- dap-show-memory-image-at-point's pure, buffer/widget-free
// half. A hex dump (BufferView::BuildMemoryBuffer) is precise but doesn't
// scale past a couple hundred bytes; rendering the same MemoryBlock as a
// small grayscale image instead makes repeating structures, zero-fill runs,
// and embedded ASCII text visually obvious at a glance the way gf's Data
// tab does for a raw memory region.
//
// Deliberately not a real per-device-pixel image the way Minimap.h's
// NCBLIT_PIXEL path is: rendering happens inside UI/MemoryImageView.h, a
// plain Cell-grid widget using the half-block-glyph doubling trick (two
// vertically-stacked pixels per terminal cell via foreground/background
// color on U+2580 UPPER HALF BLOCK) -- no ncvisual, no sixel/Kitty
// capability probing, no new z-order interaction with Minimap's own pixel
// plane (see ROADMAP.md's "Overlay/minimap z-order bug" history). This
// trades per-device pixel density for something that works uniformly on
// any TrueColor terminal with zero new failure modes.
//

#ifndef NED_EDITOR_MEMORYIMAGE_H
#define NED_EDITOR_MEMORYIMAGE_H

#include <cstddef>
#include <cstdint>

namespace ned::editor {

// The pixel grid a MemoryBlock's bytes lay out into, row-major, one byte
// per pixel. Chosen close to square (not a single very wide, one-pixel-tall
// strip) since a square-ish image is what actually reveals repeating
// fixed-size structures -- a run of same-size records shows up as
// horizontal banding only if the row width happens to divide the record
// size, which a square-ish default gives a much better chance of than an
// arbitrary wide strip. Capped to maxColumns (the viewer's available
// interior width in cells, one pixel per column). byteCount == 0 or
// maxColumns == 0 both produce a zeroed layout (maxColumns == 0 is treated
// as 1 internally only when byteCount > 0, so a genuinely zero-width
// viewport still reports zero rather than dividing by zero).
struct MemoryImageLayout {
    std::size_t pixelColumns = 0;
    std::size_t pixelRows    = 0;
};

[[nodiscard]] MemoryImageLayout ComputeMemoryImageLayout(std::size_t byteCount, std::size_t maxColumns);

// Plain RGB triplet -- Editor/ doesn't depend on UI/'s Color type (the
// dependency runs the other way throughout this codebase), so this is the
// hand-off shape UI/MemoryImageView.h wraps into a real ui::Color.
struct MemoryImageColor {
    std::uint8_t r = 0, g = 0, b = 0;
};

// Grayscale by raw value (r == g == b == byte): zero-fill reads as solid
// black bands, printable ASCII text (0x20-0x7E, the middle third of the
// byte range) as a mid-gray band, high/pointer-like bytes as light
// gray/white -- the simplest mapping that preserves every bit of
// information the hex dump already shows, just perceptible as pattern
// instead of read digit-by-digit.
[[nodiscard]] MemoryImageColor ByteToGrayscale(std::uint8_t byte);

} // namespace ned::editor

#endif // NED_EDITOR_MEMORYIMAGE_H
