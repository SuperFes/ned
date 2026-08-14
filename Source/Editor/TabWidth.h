//
// The display width (in columns) a tab character (U+0009) expands to when
// rendered (see BufferView::paint()). One process-wide setting, not
// per-mode/per-buffer -- the same "one process-wide choice" scope cut
// Theme selection and FormatOnSave's command already make. Defaults to 4;
// configured from Janet (ned/set-tab-width).
//
// Purely a *display* setting: the underlying buffer content keeps its real
// tab byte(s) untouched, only the rendered column position changes. Not to
// be confused with esc::Key::Tab / SpecialKey::Tab, the keyboard key -- this
// is about a literal U+0009 codepoint already present in a buffer's content.
//

#ifndef NED_EDITOR_TABWIDTH_H
#define NED_EDITOR_TABWIDTH_H

namespace ned::editor {

void              SetTabWidth(int columns);
[[nodiscard]] int TabWidth();

} // namespace ned::editor

#endif // NED_EDITOR_TABWIDTH_H
