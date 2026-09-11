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

// **Off by default**, and that is a judgement rather than a measurement.
//
// Everything measurable about it is cheap. It tints the *glyphs* that were
// edited rather than washing the row behind them, which is the difference
// between one changed cell per frame and a hundred and sixty -- and since
// Notcurses emits only what changed, that is the number that decides what an
// animation costs. Its thread is created once and parked on a condition
// variable (UI/EventLoop.h's AnimationTimer); the render thread never
// creates, joins or waits on anything. Per-cell it is one bool test when
// nothing is fading. Typing cost measures the same with it on as off.
//
// It is still off, because it was reported as making the editor feel slow to
// type in more than once, and an editor that feels slow is slow. A decoration
// does not get to spend the benefit of the doubt against that. Turn it on
// with (ned/set-recency-glow true).
//
// If it still feels wrong when enabled, the remaining suspect is not this
// file: Screen::Flush writes every cell of two planes every frame, so *any*
// clock-driven repaint costs a full-grid write however few cells it changes.
// A dirty-region flush is the fix, and it would speed up ordinary editing
// too. See ROADMAP.md.
void               SetRecencyGlowEnabled(bool enabled);
[[nodiscard]] bool RecencyGlowEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_RECENCYGLOW_H
