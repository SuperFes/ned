//
// project-undo follow-up: coordinates Text/UndoTree navigation across every
// file a single multi-file LSP edit (a cross-file rename, a cross-file code
// action) touched, so one `undo`/`redo` command invocation can roll every
// touched file back or forward together instead of leaving sibling files
// stuck renamed after undoing just the file point happens to be in.
//
// Deliberately in-memory only, scoped to one process's lifetime -- unlike
// each Buffer's own UndoTree (persisted per-buffer by Editor/PersistentUndo.h),
// nothing here survives a restart, and a buffer closed and reopened since a
// transaction was recorded simply can't be folded back into it (its
// UndoTree, and every sequence identity in it, is gone with the Buffer that
// owned it).
//

#ifndef NED_EDITOR_PROJECTUNDO_H
#define NED_EDITOR_PROJECTUNDO_H

#include <cstddef>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include "Text/BufferList.h"

namespace ned::editor {

// One file's half of a project-wide edit transaction: which buffer (by
// path -- a raw Buffer* would dangle across a close/reopen), and the
// Text/UndoTree.h sequence (Buffer::CurrentUndoSequence()) it sat on
// immediately before and after the edit was applied. Path, not Buffer*, is
// also what lets Undo()/Redo() re-resolve a buffer that was closed and
// reopened in between (a fresh Buffer, but the same file) as "diverged"
// rather than dangling.
struct ProjectUndoRecord {
    std::filesystem::path path;
    std::size_t           beforeSequence = 0;
    std::size_t           afterSequence  = 0;
};

struct ProjectEditTransaction {
    std::string                    description; // e.g. "Rename to 'foo' (3 files)"
    std::vector<ProjectUndoRecord> records;
};

// Outcome of a project-wide Undo()/Redo() call, for the caller (Commands.cpp's
// undo/redo, via BufferView) to turn into a status/echo-area message.
struct ProjectUndoOutcome {
    std::string              description;
    std::size_t              totalCount   = 0; // records in the transaction
    std::size_t              appliedCount = 0;
    std::vector<std::string> divergedNames; // buffer names skipped: closed, or edited separately since
};

class ProjectUndoManager {
  public:
    // Records a just-applied multi-file edit. Pushing clears the redo
    // stack (standard undo/redo semantics). A transaction touching fewer
    // than two files isn't worth tracking at the project level -- a
    // single-file edit is already exactly what that buffer's own
    // Undo()/Redo() does -- so RecordTransaction silently drops one.
    void RecordTransaction(ProjectEditTransaction transaction);

    // Whether `buffer`'s *current* undo sequence is exactly the tail this
    // buffer was left on by the top-of-undo-stack (IsUndoTarget) or
    // top-of-redo-stack (IsRedoTarget) transaction -- what lets
    // Commands.cpp's undo/redo commands decide, per invocation, whether to
    // delegate to Undo()/Redo() below instead of calling
    // Buffer::Undo()/Redo() directly. False whenever the relevant stack is
    // empty, `buffer` has no path, or its path isn't in that transaction.
    [[nodiscard]] bool IsUndoTarget(const text::Buffer& buffer) const;
    [[nodiscard]] bool IsRedoTarget(const text::Buffer& buffer) const;

    [[nodiscard]] bool CanUndo() const;
    [[nodiscard]] bool CanRedo() const;

    // Undoes/redoes the top transaction across every buffer it touched
    // that's (a) still open under `bufferList` and (b) still exactly on
    // the sequence this transaction left it on. Anything else -- closed
    // since, or edited again on its own since -- is skipped and reported
    // in the returned outcome rather than forced or guessed at. Always
    // pops the top transaction and moves it to the opposite stack
    // regardless of how many of its records actually applied: once a
    // transaction no longer fully applies, leaving it sitting on top would
    // only ever block plain per-buffer undo on every other file without
    // ever becoming valid again. A no-op (empty outcome) if the relevant
    // stack is empty -- callers are expected to guard with CanUndo()/
    // CanRedo() or IsUndoTarget()/IsRedoTarget() first.
    ProjectUndoOutcome Undo(text::BufferList& bufferList);
    ProjectUndoOutcome Redo(text::BufferList& bufferList);

  private:
    std::deque<ProjectEditTransaction> undoStack_;
    std::deque<ProjectEditTransaction> redoStack_;
};

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTUNDO_H
