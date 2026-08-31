//
// huge-file-structural-gutters follow-up: the byte margin BufferView expands
// the visible viewport by, on each side, when computing the bounded window
// it feeds mode_.fold/mode_.symbolKind/mode_.testDiscovery for a huge
// (ITextStorage::IsHuge()) buffer -- these closures otherwise take the
// buffer's *entire* text and run a full tree-sitter parse over it (Mode.cpp's
// shared IncrementalParseCache), unbounded by document size. Configured from
// Janet via ned/set-huge-structural-window-bytes, mirroring
// HighlightSettings.h's exact mutex-guarded-static-state pattern. Has no
// effect at all on an ordinary (non-huge) buffer, which is never windowed.
//

#ifndef NED_EDITOR_HUGESTRUCTURALWINDOW_H
#define NED_EDITOR_HUGESTRUCTURALWINDOW_H

#include <cstddef>

namespace ned::editor {

void                      SetHugeStructuralWindowBytes(std::size_t bytes);
[[nodiscard]] std::size_t HugeStructuralWindowBytes(); // default 4 MiB

} // namespace ned::editor

#endif // NED_EDITOR_HUGESTRUCTURALWINDOW_H
