//
// Debugging wishlist: watch-history sparkline / array-value graph (see
// ROADMAP.md). A pure, buffer-free helper pair -- no DapManager/BufferView
// dependency -- shared by DapManager::RefreshWatchHistory (numeric values
// collected across successive stops) and BufferView::ToggleWatchGraphAtPoint
// (a numeric array watch's current elements, one-shot). Renders as a
// compact single-line Unicode block-glyph sparkline (the standard
// terminal-sparkline technique) rather than Minimap.h's pixel-raster/
// ncvisual approach -- the *debug* buffer is plain Cell-grid text, not a
// pixel-capable widget, so there's no ncplane to blit into at an arbitrary
// buffer line.
//

#ifndef NED_EDITOR_SPARKLINE_H
#define NED_EDITOR_SPARKLINE_H

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace ned::editor {

// True (and sets out) iff the whole of text, trimmed of leading/trailing
// whitespace, parses as a single number -- e.g. "42", "-3.5", "1e6"; not
// "42abc", "1, 2", "true", "0x2a" (hex is deliberately not recognized here,
// unlike some watch values -- decimal/scientific only). Used to decide
// whether a watch/variable value belongs in a sparkline at all, so it's
// intentionally strict rather than a best-effort scrape.
[[nodiscard]] bool TryParseNumeric(std::string_view text, double& out);

// Renders values as a compact single-line block-glyph sparkline (U+2581
// LOWER ONE EIGHTH BLOCK .. U+2588 FULL BLOCK, 8 height levels), linearly
// normalized against their own min/max -- a flat/constant series renders as
// a steady mid-height line, not a division-by-zero special case. More than
// maxWidth values are downsampled by bucket-averaging (every input value
// contributes to exactly one bucket) rather than truncated, so a long
// history still shows its overall shape, not just its tail. Empty input (or
// maxWidth == 0) returns an empty string.
[[nodiscard]] std::string BuildBlockSparkline(std::span<const double> values, std::size_t maxWidth = 40);

} // namespace ned::editor

#endif // NED_EDITOR_SPARKLINE_H
