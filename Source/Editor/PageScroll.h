//
// The fraction of the viewport height a page up/down moves. One process-wide
// setting, mutex-guarded static state mirroring TabWidth.h's exact shape.
// Defaults to 0.65 -- Emacs' own default (`next-screen-context-lines` = 2
// lines of overlap) works out to roughly this same ballpark for a typical
// terminal height. Configured from Janet (ned/set-page-scroll-fraction).
//

#ifndef NED_EDITOR_PAGESCROLL_H
#define NED_EDITOR_PAGESCROLL_H

namespace ned::editor {

// Clamped to (0, 1] -- a non-positive fraction would never advance the
// viewport at all, and anything past 1 would scroll past a full page.
void                 SetPageScrollFraction(double fraction);
[[nodiscard]] double PageScrollFraction();

} // namespace ned::editor

#endif // NED_EDITOR_PAGESCROLL_H
