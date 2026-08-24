//
// Whether Buffer::Save/SaveToFile strip trailing whitespace from every line
// and collapse trailing blank lines at end-of-file, matching most editors'
// own "trim on save" default (VSCode's files.trimTrailingWhitespace +
// files.trimFinalNewlines, Emacs' ws-butler/delete-trailing-whitespace).
// Process-wide, mutex-guarded static state, mirroring FinalNewline.h's
// exact pattern -- including the same default-on and disk-only reasoning
// (see that header's own doc comment for why a save-time-only transform
// must never become a real, undoable buffer edit). Configured from Janet
// via ned/set-trim-trailing-whitespace-on-save.
//
// One combined setting, not two: "remove trailing spaces and extra
// newlines at the end of a file" was raised as a single ask, and the two
// only ever compose (trailing blank lines are just consecutive newlines
// left over once each line's own trailing whitespace is already gone).
//

#ifndef NED_EDITOR_TRIMONSAVE_H
#define NED_EDITOR_TRIMONSAVE_H

namespace ned::editor {

void               SetTrimTrailingWhitespaceOnSave(bool enabled);
[[nodiscard]] bool TrimTrailingWhitespaceOnSave();

} // namespace ned::editor

#endif // NED_EDITOR_TRIMONSAVE_H
