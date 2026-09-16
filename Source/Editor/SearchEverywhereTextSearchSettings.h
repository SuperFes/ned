//
// search-everywhere-symbols-and-text follow-up: whether search-everywhere's
// Text category (a debounced, backgrounded Editor::SearchDirectory scan) is
// allowed to run at all. Same mutex-guarded-static-state shape as
// SearchEverywhereGestureSettings.h/TabWidth.h.
//
// On by default, but -- like the double-tap-Shift gesture -- worth its own
// escape hatch: this is the one part of search-everywhere backed by
// brand-new background-threading plumbing (a detached std::thread, an
// alive-flag checked before the Post-marshaled result touches BufferView)
// rather than reuse of an already-shipped, already-trusted subsystem the
// way the Symbol category's workspace/symbol half is. Actions/Macros/Files/
// Buffers/Symbols are unaffected by this setting.
//

#ifndef NED_EDITOR_SEARCHEVERYWHERETEXTSEARCHSETTINGS_H
#define NED_EDITOR_SEARCHEVERYWHERETEXTSEARCHSETTINGS_H

namespace ned::editor {

void               SetSearchEverywhereTextSearchEnabled(bool enabled);
[[nodiscard]] bool SearchEverywhereTextSearchEnabled(); // default true

} // namespace ned::editor

#endif // NED_EDITOR_SEARCHEVERYWHERETEXTSEARCHSETTINGS_H
