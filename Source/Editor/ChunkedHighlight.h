//
// perf/parallel-highlighting-round-1 follow-up: the pure, one-chunk-at-a-time
// step behind Minimap's background highlight sweep (UI/Minimap.cpp's
// AdvanceHighlightSweep) -- split out so it's unit-testable with no
// EventLoop/DeadlineTimer/Notcurses involved at all, and so a test can drive
// it synchronously in a loop rather than needing a real timer to tick.
//
// The problem this exists for: a Mode's highlight() always parses/queries
// the *whole* document regardless of the HighlightWindow it's given (only
// capture EMISSION is bounded by the window -- see HighlightWindow's own
// doc comment in Mode.h), which is what makes calling it with a small window
// cheap relative to the shared parse it rides on (IncrementalParseCache::
// Update is a fast text-equality hit for every call after the first, as long
// as the exact same text is passed each time). Minimap's own whole-document
// call is the one structurally unwindowed consumer -- unlike BufferView,
// which only ever asks for its own viewport -- and was measured (KEYBENCH,
// 2026-09-13) at ~20ms/keystroke-burst-end on a 190KiB injection-heavy
// markdown file, entirely attributable to this one call, since markdown
// injects markdown_inline into every inline node and each injected region
// is its own sub-parse. HighlightSweepChunk lets a caller pay that same
// total cost in small windowed slices spread across many calls instead of
// one blocking call.
//

#ifndef NED_EDITOR_CHUNKEDHIGHLIGHT_H
#define NED_EDITOR_CHUNKEDHIGHLIGHT_H

#include <cstddef>
#include <string_view>
#include <vector>

#include "Mode.h"

namespace ned::editor {

// Runs `highlight` over the window [cursor, min(cursor + chunkBytes,
// text.size())), appending to `out` exactly the returned spans that start
// within that window (span.startByte >= cursor), and returns the new
// cursor (the window's own end). Skips a span that starts before `cursor`
// on purpose: such a span intersects this window (which is how it got
// returned at all -- HighlightWindow bounds emission by intersection, not
// containment) but was already captured, in full, by the earlier chunk
// whose own window is what its startByte actually falls inside -- an
// invariant that holds as long as callers advance `cursor` by exactly the
// chunk size actually processed (this function's own return value) rather
// than skipping or overlapping windows themselves.
//
// `chunkBytes` must be > 0; `cursor` should not exceed text.size() (a
// caller that has already reached the end has nothing left to sweep).
//
// Calling this repeatedly from cursor = 0 until the returned cursor reaches
// text.size(), with `out` threaded through unchanged, accumulates the exact
// same span set `highlight(text, HighlightWindow{})` (i.e. windowed over the
// whole document in one call) would have produced -- pinned by
// Tests/ChunkedHighlightTest.cpp across chunk sizes and both a plain and an
// injection-heavy document, since that equivalence is the whole point: the
// chunking must be a scheduling change only, never a rendering one.
[[nodiscard]] std::size_t HighlightSweepChunk(const HighlightFunction& highlight, std::string_view text,
                                              std::size_t cursor, std::size_t chunkBytes,
                                              std::vector<HighlightSpan>& out);

} // namespace ned::editor

#endif // NED_EDITOR_CHUNKEDHIGHLIGHT_H
