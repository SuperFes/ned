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

// How long one edit's glow takes to fade to nothing.
//
// 600ms was the first try and was much too slow, reported live. The reason
// is not really the fade's own speed: at 600ms an ordinary typing rate keeps
// six or seven glows alive at once, so the effect stops reading as "that
// character just landed" and becomes a smear trailing the cursor that never
// settles while you type. At 200ms roughly two overlap, the trail stays
// close to the caret, and the screen is still between bursts.
inline constexpr std::chrono::milliseconds kRecencyGlowDuration{200};

// One animation frame. ~7 repaints across a whole glow, which reads as a
// smooth fade at terminal refresh rates while costing an order of magnitude
// less than a real per-frame render loop -- and only for the 200ms a glow
// actually lasts.
inline constexpr std::chrono::milliseconds kRecencyGlowInterval{28};

// On by default. It was off for a while, and the history is worth keeping,
// because the reason had nothing to do with this feature.
//
// It was blamed, repeatedly and in good faith, for making the editor feel
// slow to type in. Three genuine defects were found and fixed while chasing
// that -- a per-cell mutex and clock read, a thread spawned and joined per
// animation tick, then per keystroke -- and none of them was the cause. The
// cause was syntax highlighting running unbounded over the whole document on
// every keystroke: 134ms per keystroke on a 128 KiB markdown file, against
// about 1ms for this. With that fixed (~25ms), typing measures the same with
// the glow on as off: 24,588us against 24,650us, which is noise.
//
// What kept it cheap on its own terms: it tints the *glyphs* that were
// edited rather than washing the row behind them, so an animation frame
// disturbs one or two cells instead of a hundred and sixty, and Notcurses
// emits only what changed. Its thread is created once and parked on a
// condition variable (UI/EventLoop.h's AnimationTimer) -- the render thread
// never creates, joins or waits on one.
//
// The lesson, since it cost three rounds: whole-process CPU said "fine"
// through all of it. Time the keystroke path (Tests/KeystrokeBench.cpp), not
// the process.
void               SetRecencyGlowEnabled(bool enabled);
[[nodiscard]] bool RecencyGlowEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_RECENCYGLOW_H
