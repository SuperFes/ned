//
// loose-ends follow-up: the buffer-size ceiling (bytes) above which
// BufferView skips syntax highlighting entirely -- was BufferView.cpp's own
// hardcoded kMaxHighlightBytes (8 MiB), grown into a process-wide setting
// (mutex-guarded static state, mirroring TabWidth.h's exact pattern)
// exactly the way TabWidth/Theme selection's own hardcoded defaults once
// did. Configured from Janet via ned/set-max-highlight-bytes; 0 disables
// highlighting for every buffer, which falls out of the comparison rather
// than being a special case.
//

#ifndef NED_EDITOR_HIGHLIGHTSETTINGS_H
#define NED_EDITOR_HIGHLIGHTSETTINGS_H

#include <cstddef>

namespace ned::editor {

void                      SetMaxHighlightBytes(std::size_t bytes);
[[nodiscard]] std::size_t MaxHighlightBytes(); // default 8 MiB

} // namespace ned::editor

#endif // NED_EDITOR_HIGHLIGHTSETTINGS_H
