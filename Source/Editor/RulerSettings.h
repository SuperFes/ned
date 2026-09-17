//
// Print-margin/fill-column-indicator follow-up. Two process-wide settings
// for UI/BufferView.h's own ruler paint -- whether it's shown at all, and
// which buffer column it marks. Mutex-guarded static state, mirroring
// TabWidth.h/.cpp's exact pattern (see also MinimapSettings.h's own
// enabled+numeric pair for the closest two-setting precedent).
//

#ifndef NED_EDITOR_RULERSETTINGS_H
#define NED_EDITOR_RULERSETTINGS_H

namespace ned::editor {

// Default true: a passive, always-visible reference line costs nothing to
// have on and is what "draw a line at 80" actually asked for; toggle off
// for a theme/workflow that doesn't want it.
void               SetRulerEnabled(bool enabled);
[[nodiscard]] bool RulerEnabled();

// The 0-indexed buffer column the ruler marks. Default 80, the classic
// print-margin width -- independent of .clang-format's own ColumnLimit: 0
// (CLAUDE.md: "no column limit"), which is a formatter decision about this
// codebase's own C++ source, not a claim that line width stops mattering
// for prose/other-language buffers a user opens. Non-positive values are
// clamped to 1, the same "can't be zero-or-negative" convention
// TabWidth/MinimapWidth already establish.
void              SetRulerColumn(int column);
[[nodiscard]] int RulerColumn();

} // namespace ned::editor

#endif // NED_EDITOR_RULERSETTINGS_H
