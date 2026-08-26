//
// Minimap widget follow-up. Three process-wide settings for
// UI/Minimap.h/.cpp -- whether it's shown at all, how many columns wide
// it is, and how many real buffer columns each rendered "pixel" (braille
// sub-dot) represents. Mutex-guarded static state, mirroring
// TabWidth.h/.cpp's exact pattern.
//

#ifndef NED_EDITOR_MINIMAPSETTINGS_H
#define NED_EDITOR_MINIMAPSETTINGS_H

namespace ned::editor {

// Default true -- WindowManager::Pane reads this once, at construction, to
// seed which of scrollColumn_/minimap_ starts active; toggle-minimap
// (C-c m) flips the running state afterward, independent of this default.
void              SetMinimapEnabled(bool enabled);
[[nodiscard]] bool MinimapEnabled();

// Columns wide (not sub-dot columns -- each column packs 2 braille
// sub-columns). Default 5, matching the user's own "4 or 5 wide" framing;
// non-positive values are clamped to 1, the same "can't be zero-or-
// negative" convention TabWidth's own setter already established.
void              SetMinimapWidth(int columns);
[[nodiscard]] int MinimapWidth();

// How many real buffer columns one braille sub-dot column represents.
// Default 8 -- not a compression ratio: a line longer than
// 2*MinimapWidth()*MinimapCharsPerDot() real columns simply isn't rendered
// past that point. Raised from an initial default of 2 (the user's own
// original "1 or 2 chars per pixel" framing) after seeing it in a real
// terminal: at 2, most real code filled the bar edge-to-edge with dots,
// reading as visually "overly filled" rather than showing genuine
// per-line shape; 8 leaves the right side of the bar mostly empty for
// typical line lengths, closer to how a real minimap should read at a
// glance. A floating-point value (minimap-fractional-chars-per-dot
// follow-up) so a value between two whole-number steps (e.g. 8.5) is
// reachable too, not just the coarser integer jumps -- real-pixel mode in
// particular has enough columns of headroom for the difference to actually
// show. Values <= 0 clamped to 1.
void                 SetMinimapCharsPerDot(double columns);
[[nodiscard]] double MinimapCharsPerDot();

} // namespace ned::editor

#endif // NED_EDITOR_MINIMAPSETTINGS_H
