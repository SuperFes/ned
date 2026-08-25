//
// The one process-wide "is Vim emulation on" flag -- TabWidth.h's own mutex-guarded
// static-storage shape, same "setter, getter, ned/set-* Janet binding" convention every
// process-wide setting in this codebase follows. Default false: the user's own personal
// preference is Emacs-style bindings, this exists for adoption reasons (see ROADMAP.md),
// not because it's on by default for anyone.
//

#ifndef NED_EDITOR_VIM_VIMSETTINGS_H
#define NED_EDITOR_VIM_VIMSETTINGS_H

namespace ned::editor::vim {

void               SetVimModeEnabled(bool enabled);
[[nodiscard]] bool VimModeEnabled();

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMSETTINGS_H
