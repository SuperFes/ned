//
// Cross-session undo history (persistent-undo follow-up, ROADMAP.md's
// "No persistent/cross-session undo" item): a per-file undo tree survives
// closing and reopening ned, stored as one JSON file per buffer path under
// $XDG_STATE_HOME/ned/undo/ -- disposable editor state, same XDG base
// directory as Session.h/Backup.h, same per-path-hash filename recipe as
// Backup.h's own BackupDirectoryForFile.
//
// The hard problem this exists to solve safely: a file can change on disk
// between ned runs (edited by another tool, or by the user choosing not to
// save before quitting), at which point the persisted tree no longer
// describes the buffer that's about to be loaded. Rather than trying to
// reconcile that (a three-way merge into historical undo nodes -- see
// ROADMAP.md's Maybelist entry for why that's deliberately deferred), this
// is a hash-gate: every node's full content is stored (Text/UndoTree.h's
// own "full snapshot, not diff" philosophy carried to disk), and on load
// the freshly-read file content is compared directly against every
// persisted node, not just the tree's tip -- the common "quit without
// saving" case leaves disk matching an *ancestor* of the tip, not the tip
// itself, and tip-only comparison would wrongly discard the whole tree in
// that case. A match anywhere restores the full tree with point set to
// that matching node; no match at all discards the persisted file's
// history outright and the buffer starts with a fresh single-node tree,
// exactly like a normal first-time open.
//
// No merge, no partial recovery, no node-count cap in v1 -- see this
// header's own settings below for the one size-based cutoff that does
// exist (a large file's undo tree multiplies by node count, unlike a
// single Backup.h version).
//

#ifndef NED_EDITOR_PERSISTENTUNDO_H
#define NED_EDITOR_PERSISTENTUNDO_H

#include <filesystem>

#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor {

// $XDG_STATE_HOME/ned/undo (falling back to ~/.local/state/ned/undo).
// A pure path calculation, mirroring BackupsDirectory() exactly -- throws
// std::runtime_error if neither XDG_STATE_HOME nor HOME is set.
[[nodiscard]] std::filesystem::path UndoDirectory();

// UndoDirectory() / (Fnv1a64Hex(FilePlaceStore::NormalizePathKey(file)) +
// ".json") -- one file per buffer path, unlike Backup.h's per-file
// directory-of-versions (there's only ever one current tree to persist, no
// history-of-histories). Pure calculation.
[[nodiscard]] std::filesystem::path UndoFileForPath(const std::filesystem::path& file);

// Persists buffer's whole undo tree, if it's eligible and worth writing:
// silently skipped when persistent undo is disabled, the buffer has no
// path, is still IsLoading(), is ReadOnly(), lives in ScratchDirectory()
// (out of scope -- ScratchPad.h owns scratch persistence), its content
// exceeds PersistentUndoMaxSizeMb(), or its undo tree is still just the
// root (nothing to restore beyond a fresh load already gives). Writes via
// the usual sibling-.ned-tmp-then-rename atomic write. Swallows every
// failure -- an unattended periodic save with nothing to report to, same
// posture as AutoSaveFileBuffers.
void SaveUndoHistory(const text::Buffer& buffer);

// The load-time counterpart: reads buffer's persisted undo file, if any,
// and restores it (Buffer::RestoreUndoTree) only if some node's content
// exactly matches buffer's current content (see this header's own doc
// comment for why every node, not just the tip, is checked). Silently
// does nothing on a missing/malformed file, when disabled, or when no node
// matches -- the buffer is left with whatever fresh single-node tree it
// already has, same as an ordinary first-time open. Same eligibility
// filter as SaveUndoHistory (path/IsLoading/ReadOnly/scratch), minus the
// size cutoff (restoring is one read, not a growing periodic write).
void TryRestoreUndoHistory(text::Buffer& buffer);

// The periodic-tick rider (AutoSaveFileBuffers' analogue): calls
// SaveUndoHistory for every buffer in bufferList whose ContentGeneration()
// has actually changed since this process last persisted its undo history
// (a mutex-guarded memo, same reasoning as AutoSaveFileBuffers' own --
// undo/redo navigation and every content edit alike bump
// ContentGeneration(), so it's an exact proxy for "the tree or its current
// node may have changed"). Also the function main.cpp's quit path calls
// once more, unconditionally eligible buffers included -- no separate
// "force" parameter needed the way SaveFilePlaces has one, because this
// memo is precise per-buffer rather than one dirty bit for a whole store:
// if nothing changed since the last periodic tick, disk already agrees.
void SaveUndoHistoryForOpenBuffers(text::BufferList& bufferList);

// Process-wide settings (mutex-guarded static state, mirroring
// Backup.h's exact pattern), configured from Janet:
// ned/set-persistent-undo, ned/set-persistent-undo-max-size-mb.
void               SetPersistentUndoEnabled(bool enabled);
[[nodiscard]] bool PersistentUndoEnabled(); // default true
void               SetPersistentUndoMaxSizeMb(int megabytes);
[[nodiscard]] int  PersistentUndoMaxSizeMb(); // default 16; non-positive values are clamped to 1

// Tests only: settings back to default and the generation memo cleared --
// process-wide statics leak between Catch2 cases otherwise
// (ResetBackupsForTesting's convention).
void ResetPersistentUndoForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_PERSISTENTUNDO_H
