//
// Translucency phase 6: the recency glow -- a brief wash over text that was
// just edited, fading out on its own. InlineDiagnostics.h's exact pattern
// for the on/off switch, plus the two tunables the animation needs.
//
// The animation is the reason this file carries timings at all. ned's event
// loop deliberately has no free-running render tick: it wakes for real input
// or for work posted to it, and nothing else. So a fading glow re-arms a
// one-shot timer per frame *while a glow is still live* and stops the moment
// the last one expires -- an idle editor costs exactly zero wakeups, which is
// the whole reason this is safe to have on by default. The background-
// activity spinner (BackgroundActivity.h) already works this way; these two
// constants are its kBackgroundActivitySpinnerInterval, restated for a fade
// rather than a spinner.
//

#ifndef NED_EDITOR_RECENCYGLOW_H
#define NED_EDITOR_RECENCYGLOW_H

#include <chrono>

namespace ned::editor {

// How long one edit's glow takes to fade to nothing. Long enough to notice
// out of the corner of an eye while typing, short enough that it is gone
// before it becomes the thing you are looking at.
inline constexpr std::chrono::milliseconds kRecencyGlowDuration{600};

// One animation frame. 60ms is ~10 repaints across a whole glow, which reads
// as a smooth fade at terminal refresh rates while costing an order of
// magnitude less than a real per-frame render loop -- and only for the
// 600ms a glow actually lasts.
inline constexpr std::chrono::milliseconds kRecencyGlowInterval{60};

void               SetRecencyGlowEnabled(bool enabled);
[[nodiscard]] bool RecencyGlowEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_RECENCYGLOW_H
