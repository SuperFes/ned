//
// The one process-wide "which keybinding convention is active" setting --
// StatusGutterSettings.h's own mutex-guarded-enum shape. Emacs is the
// default keymap BuildDefaultGlobalKeymap builds; Vim and Modern are each a
// live-checked override consulted ahead of it (BufferView::OnKeyEvent),
// never a rebuild of the Keymap object itself. SetKeymapStyle is the one
// place that also drives vim::ModeEnabled() (Vim/Settings.h) -- that flag
// stays the actual runtime switch the Vim engine polls every keystroke, so
// setting it here rather than duplicating "is vim active" as two facts that
// could disagree.
//

#ifndef NED_EDITOR_KEYMAPSTYLE_H
#define NED_EDITOR_KEYMAPSTYLE_H

#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

enum class KeymapStyle {
    Emacs,  // default: BuildDefaultGlobalKeymap alone
    Vim,    // vim::ModeEnabled() true underneath -- see Vim/Engine.h
    Modern, // BuildModernOverrideKeymap (Commands.h) consulted ahead of the Emacs default
};

void                      SetKeymapStyle(KeymapStyle style);
[[nodiscard]] KeymapStyle GetKeymapStyle();

// "emacs"/"vim"/"modern", lowercase, the same spellings ned/set-keymap-style
// and --keymap-style both accept. nullopt for anything else -- the caller
// picks its own wording for the resulting error (a Janet exception message,
// a CLI11 validator).
[[nodiscard]] std::optional<KeymapStyle> ParseKeymapStyle(std::string_view name);
[[nodiscard]] std::string_view           KeymapStyleName(KeymapStyle style);

} // namespace ned::editor

#endif // NED_EDITOR_KEYMAPSTYLE_H
