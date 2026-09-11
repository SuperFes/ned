//
// One per-buffer cache of a Mode's raw highlight spans, shared by everything
// that needs them.
//
// BufferView and Minimap each kept their own, keyed the same way and filled
// by the same `mode.highlight(buffer.Text())` call -- so with the minimap on
// (the default) every keystroke ran the whole-document highlight *twice*.
// Measured on a 125 KiB markdown file: 67ms once, 134ms twice, per keystroke.
//
// Keyed on the facts that can change what highlighting should produce with no
// edit at all: the buffer's content generation, the capture-class generation
// (ned/set-capture-class and friends), and the mode's name. LSP semantic
// tokens are deliberately *not* part of this -- they are appended by
// BufferView on top of this result, and the minimap does not show them, so
// keying on them here would make the two consumers miss each other's entries
// for no gain.
//
// Returns a shared_ptr so a consumer can hold the spans without copying them
// (BufferView used to copy the whole vector every frame even on a cache hit)
// and without caring when the cache evicts the entry.
//
// Main-thread only, like every paint path that uses it.
//

#ifndef NED_EDITOR_HIGHLIGHTCACHE_H
#define NED_EDITOR_HIGHLIGHTCACHE_H

#include <memory>
#include <vector>

#include "Mode.h"
#include "Text/Buffer.h"

namespace ned::editor {

// Never null; an empty span list for a mode with no highlight function.
[[nodiscard]] std::shared_ptr<const std::vector<HighlightSpan>> CachedHighlightSpans(const text::Buffer& buffer,
                                                                                     const Mode&         mode);

// Drops every entry -- a reset seam for tests, and for anything that
// invalidates highlighting wholesale.
void ClearHighlightCache();

} // namespace ned::editor

#endif // NED_EDITOR_HIGHLIGHTCACHE_H
