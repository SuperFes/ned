//
// Diff gutter markers follow-up: how long BufferView's diffRefreshTimer_
// waits, after the last content-changing edit, before re-requesting a `git
// diff` for the current buffer -- a debounce, not a poll interval, so rapid
// typing keeps pushing the deadline out rather than spawning a subprocess
// per keystroke. One process-wide setting, mutex-guarded static state
// mirroring TabWidth.h's exact shape. Configured from Janet
// (ned/set-diff-refresh-debounce-ms).
//

#ifndef NED_EDITOR_DIFFREFRESHSETTINGS_H
#define NED_EDITOR_DIFFREFRESHSETTINGS_H

#include <chrono>

namespace ned::editor {

// Non-positive values are clamped to 1ms rather than rejected, same
// convention as TabWidth::SetTabWidth.
void                                    SetDiffRefreshDebounceMs(int milliseconds);
[[nodiscard]] std::chrono::milliseconds DiffRefreshDebounce(); // default 1200ms

} // namespace ned::editor

#endif // NED_EDITOR_DIFFREFRESHSETTINGS_H
