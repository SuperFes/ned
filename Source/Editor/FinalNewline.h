//
// Whether Buffer::Save/SaveToFile ensure the file they write ends with a
// trailing '\n', matching most editors' own "insert final newline on save"
// default (Emacs' require-final-newline, VSCode's files.insertFinalNewline).
// Process-wide, mutex-guarded static state, mirroring ScratchPad.h's
// SetScratchAutoSaveEnabled/ScratchAutoSaveEnabled bool pattern exactly.
// Default on, matching the user's own explicit ask. Configured from Janet
// via ned/set-ensure-final-newline.
//
// Deliberately disk-only, not a live buffer edit: Buffer::SaveToFile adds
// the trailing byte only to what gets written to the temp file, never to
// Rope_ itself -- Point_/Mark_/Modified_/ContentGeneration_/the undo tree
// are all completely unaffected by a save. A version that instead called
// Buffer::InsertAt to make it a real, visible, undoable edit was considered
// and rejected: it would push a save-time-only edit onto the undo tree the
// user never typed, meaning the very next Undo after a save would undo the
// newline instead of the user's actual last real edit -- confirmed as a
// real problem by walking this codebase's own existing
// "Undo/redo mark the buffer modified..." test (Tests/BufferTest.cpp)
// through that design, not just anticipated in the abstract. This way, a
// buffer's live content only ever changes because of a real, explicit edit;
// only the file on disk (and a subsequent Buffer::FromFile reload of it)
// ever gains the byte the buffer itself didn't have.
//

#ifndef NED_EDITOR_FINALNEWLINE_H
#define NED_EDITOR_FINALNEWLINE_H

namespace ned::editor {

void               SetEnsureFinalNewline(bool enabled);
[[nodiscard]] bool EnsureFinalNewline();

} // namespace ned::editor

#endif // NED_EDITOR_FINALNEWLINE_H
