//
// configurable-formatter follow-up: whether save-buffer/save-buffer-force
// run Editor/ScopedFormat.h's scoped Native reindent+rule+Hygiene pass
// before writing to disk, when no external FormatCommand() and no running
// LSP server already claimed this save (see Commands.cpp's saveBufferBody
// and its own shouldDeferToLspFormat -- External and LSP both keep their
// existing precedence over this, unchanged). Process-wide, mutex-guarded
// static state, mirroring TrimOnSave.h/FinalNewline.h's exact pattern.
// Default OFF, unlike those two -- this one can restructure a line's own
// content (reindent, rewrap, respace), not just trim/append whitespace, so
// it stays opt-in rather than a silent new default for every save.
// Configured from Janet via ned/set-auto-format-on-save.
//

#ifndef NED_EDITOR_AUTOFORMATONSAVE_H
#define NED_EDITOR_AUTOFORMATONSAVE_H

namespace ned::editor {

void               SetAutoFormatOnSave(bool enabled);
[[nodiscard]] bool AutoFormatOnSaveEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_AUTOFORMATONSAVE_H
