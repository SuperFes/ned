//
// search-everywhere follow-up: whether the double-tap-Shift gesture
// (UI/DoubleTapModifier.h) is allowed to open search-everywhere at all.
// Same mutex-guarded-static-state shape as RenameReviewSettings.h/TabWidth.h
// and the dozens of settings modules beside them.
//
// On by default, but worth an explicit escape hatch unlike most settings in
// this file's own convention: the gesture only fires under the Kitty
// keyboard protocol, which Notcurses negotiates on its own with no
// capability query this codebase can consult first (unlike pixel graphics'
// CanPixelGraphics()) -- an untested terminal/multiplexer producing a
// bare-Shift-press shape this detector didn't anticipate is a real,
// unverifiable-in-advance risk. M-s (the ordinary keybinding) is unaffected
// by this setting and always works.
//

#ifndef NED_EDITOR_SEARCHEVERYWHEREGESTURESETTINGS_H
#define NED_EDITOR_SEARCHEVERYWHEREGESTURESETTINGS_H

namespace ned::editor {

void               SetSearchEverywhereGestureEnabled(bool enabled);
[[nodiscard]] bool SearchEverywhereGestureEnabled(); // default true

} // namespace ned::editor

#endif // NED_EDITOR_SEARCHEVERYWHEREGESTURESETTINGS_H
