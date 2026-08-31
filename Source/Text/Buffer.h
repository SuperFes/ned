//
// An editable, named text buffer: a Rope plus point/mark/region and a
// per-buffer UndoTree. All editing and cursor-movement operations work in
// grapheme-cluster terms (see Grapheme.h) -- codepoints and bytes stay
// internal details.
//
// Deliberately has no dependency on KillRing: kill/yank commands are composed
// by callers (Phase 2) out of Buffer::DeleteRange/InsertAtPoint and a
// KillRing instance, keeping the two independently testable and usable.
//

#ifndef NED_TEXT_BUFFER_H
#define NED_TEXT_BUFFER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ITextStorage.h"
#include "LineEnding.h"
#include "PieceTable.h"
#include "Rope.h"
#include "UndoTree.h"

namespace ned::text {

// large-file-async-load polish: live progress for a background loader
// filling a placeholder buffer in. bytesRead is written by the loader's own
// thread and read by UI paint code, hence atomic; totalBytes is written
// once, before the loader thread ever starts, and read-only after. Held by
// Buffer via shared_ptr specifically because Buffer is moved into
// BufferList's unique_ptr on open and std::atomic isn't movable -- the
// exact plumbing constraint that kept progress loader-local until now (see
// ROADMAP.md's large-file-handling notes).
struct LoadProgress {
    std::atomic<std::uintmax_t> bytesRead{0};
    std::uintmax_t              totalBytes = 0;
};

class Buffer {
  public:
    explicit Buffer(std::string name, Rope initialContent = Rope());

    // Throws std::runtime_error if the file can't be opened for reading, or
    // BinaryFileError (BinaryDetect.h) if it looks binary and allowBinary is
    // false (the default) -- open-binary-anyway follow-up: pass true to
    // load it as text anyway (an explicit user override, e.g. a confirmed
    // "open anyway?" prompt or a --force-binary CLI flag; never set based on
    // file content itself).
    [[nodiscard]] static Buffer FromFile(const std::filesystem::path& path, bool allowBinary = false);

    // huge-file-editing follow-up: opens path via a PieceTableStorage
    // (Text/PieceTable.h) instead of reading the whole file into memory --
    // BufferList::OpenFile routes here automatically for anything over
    // HugeFileThreshold(). Throws the same BinaryFileError/allowBinary
    // contract FromFile does (a huge buffer isn't exempt from "don't open
    // binary as text" -- LooksBinary only reads the first 8 KiB, so this
    // stays cheap regardless of file size), MappedFileError
    // (Text/MappedFile.h) if the file can't be mapped, and (when allowBinary
    // is false) a plain std::runtime_error if the file contains any CRLF/CR
    // line ending -- Storage_ is always LF-only throughout this class, and
    // normalizing that for a multi-GB file would mean rewriting every line,
    // defeating the point of this path entirely; a CRLF huge file still
    // opens fine via the normal FromFile path above, just without this
    // one's memory/speed benefits. See the huge-file-editing plan for the
    // follow-up that would lift this restriction for genuine text.
    //
    // open-binary-anyway follow-up: allowBinary also skips the CR scan
    // entirely (not just the refusal) -- a confirmed binary open has no
    // line-ending semantics to protect, so a stray 0x0D is just a byte, and
    // a multi-GB binary file shouldn't pay for a second full-file streaming
    // pass to reach a conclusion that wouldn't matter anyway.
    //
    // disk-space-safety follow-up: never refuses to open on disk-space
    // grounds (a huge file should always at least be viewable) -- instead,
    // when HugeFileDiskSpaceCheckEnabled() and available free space on the
    // file's filesystem doesn't comfortably clear
    // HugeFileMinFreeSpaceMultiplier() times the file's size (Text/
    // DiskSpace.h -- the atomic-rename save pattern needs the new content's
    // full size in free space, and a copy-on-write filesystem can
    // transiently need close to double that), the returned buffer is
    // ReadOnly() with a specific ReadOnlyReason() naming the actual
    // available/required byte counts. This is a soft, overridable
    // downgrade -- toggle-read-only (or SetReadOnly(false) directly) lets a
    // user who understands the risk force it editable anyway. SaveToFile
    // below re-checks unconditionally at save time regardless of that
    // override -- see its own doc comment for why that check has no
    // override.
    [[nodiscard]] static Buffer FromHugeFile(const std::filesystem::path& path, bool allowBinary = false);

    // An empty buffer already associated with path (named after path's
    // filename, Path() returns path immediately) without reading or
    // requiring the file to exist yet -- the first Save()/SaveToFile()
    // creates it. Mirrors Emacs' "visit a nonexistent file" behavior.
    [[nodiscard]] static Buffer NewFile(std::filesystem::path path);

    // Writes to path and remembers it as the buffer's associated file.
    // Throws std::runtime_error if the file can't be opened for writing.
    // ensureFinalNewline (default true) appends a trailing '\n' to what's
    // WRITTEN, if the content is non-empty and doesn't already end with
    // one -- deliberately disk-only, this buffer's own live content
    // (Text()/Point()/Modified()/undo history) is never touched by it; see
    // Editor/FinalNewline.h's own header comment for why. trimTrailingWhitespace
    // (default true) strips trailing spaces/tabs from every line and
    // collapses trailing blank lines at end-of-file, applied before
    // ensureFinalNewline's own append -- same disk-only reasoning, see
    // Editor/TrimOnSave.h. Buffer itself stays unaware of the real,
    // configured, Janet-settable defaults the way it already does for
    // tabWidth elsewhere -- callers that want those pass
    // editor::EnsureFinalNewline()/editor::TrimTrailingWhitespaceOnSave() in
    // explicitly (Commands.cpp's save-buffer does). lineEndingOverride
    // (crlf-handling follow-up) follows the same pattern: nullopt (the
    // default) writes using this buffer's own tracked LineEndingKind()
    // unchanged (the Preserve case, needing no caller involvement at all);
    // a caller wanting Editor/LineEndingPolicy.h's Force* policy applied
    // resolves it via editor::ResolveLineEndingForSave(buffer.LineEndingKind())
    // and passes the result in here -- same "Text/ stays Editor/-unaware,
    // the caller resolves the setting" split. Either way this buffer's
    // LineEndingKind() is updated to whatever ending actually got written,
    // once the write+rename below fully succeeds.
    //
    // disk-space-safety follow-up: for a huge (piece-table-backed) buffer
    // specifically, also throws std::runtime_error (before opening the
    // .ned-tmp temp file at all, so no I/O is wasted on a write that would
    // fail partway through) when HugeFileDiskSpaceCheckEnabled() and
    // available free space doesn't comfortably clear
    // HugeFileMinFreeSpaceMultiplier() times the content size -- see
    // Text/DiskSpace.h. Unlike FromHugeFile's own open-time downgrade, this
    // check has **no override**: even a buffer someone forced back to
    // ReadOnly(false) still can't complete an unsafe save. That's the
    // actual hard guarantee this feature exists for; the open-time
    // downgrade is the soft, overridable layer.
    //
    // progressive-huge-file-load follow-up: also throws, unconditionally
    // and first (before even the disk-space check above), whenever
    // IsLoading() is true -- a still-loading huge buffer is genuinely
    // editable (see ReadOnly()'s own doc comment), so without this a
    // mid-load save could silently write a truncated, not-yet-fully-read
    // file over the real one. Also has no override.
    void SaveToFile(const std::filesystem::path& path, bool ensureFinalNewline = true, bool trimTrailingWhitespace = true,
                    std::optional<LineEnding> lineEndingOverride = std::nullopt);
    // Writes to the buffer's associated file. Throws std::runtime_error if
    // the buffer has none (i.e. FromFile/SaveToFile were never called).
    void Save(bool ensureFinalNewline = true, bool trimTrailingWhitespace = true,
              std::optional<LineEnding> lineEndingOverride = std::nullopt);

    [[nodiscard]] const std::string&                          Name() const;
    void                                                      Rename(std::string name);
    [[nodiscard]] const std::optional<std::filesystem::path>& Path() const;

    // Rebinds this buffer to a different on-disk path without touching its
    // content (project-file-ops follow-up: rename-file updates the open
    // buffer, if any, to follow the file it just renamed on disk, rather
    // than leaving it pointing at a now-nonexistent path). Does not itself
    // rename anything on disk, and does not change Name() -- callers that
    // want the buffer's name to follow too call Rename() separately.
    void SetPath(std::filesystem::path path);

    // crlf-handling follow-up: the line ending FromFile detected in the
    // on-disk file (LF for a NewFile()/never-loaded buffer), or whatever
    // SetLineEndingOverride last set. Purely metadata -- Rope_ itself is
    // always LF-only, see Text/LineEnding.h's own header comment. This is
    // what SaveToFile feeds through Editor::ResolveLineEndingForSave to
    // decide what gets written, and what ModeLine's indicator displays.
    [[nodiscard]] ned::text::LineEnding LineEndingKind() const;

    // Sets this buffer's tracked line ending, which every subsequent save
    // feeds through Editor::ResolveLineEndingForSave (still subject to a
    // Force policy -- see Editor/LineEndingPolicy.h). Two call sites: the
    // load path (FromFile/FinishLoad) records what was actually detected on
    // disk, and convert-line-endings-to-lf/-crlf/-cr (Commands.cpp) is the
    // one user-facing override. Deliberately does not touch
    // Rope_/UnsavedChangeRanges_/Modified() -- same disk-only-until-saved
    // reasoning as ensureFinalNewline/trimTrailingWhitespace: nothing on
    // disk changes until the next explicit save actually writes it out.
    void SetLineEndingOverride(ned::text::LineEnding ending);

    // Storage-agnostic: an ordinary buffer is Rope-backed underneath, a huge
    // (multi-GB) buffer is piece-table-backed (see Text/PieceTable.h,
    // Text/ITextStorage.h) -- every caller here goes through the exact same
    // interface either way. Content() itself stays cheap regardless of
    // which is active; individual ITextStorage methods document which of
    // them are safe to call unconditionally vs. which (ToString() chief
    // among them) force a full-document materialize a caller should avoid
    // for a huge buffer specifically (ITextStorage::IsHuge()).
    [[nodiscard]] const ITextStorage& Content() const;
    [[nodiscard]] std::string         Text() const;
    [[nodiscard]] std::size_t         Size() const;

    // True if the buffer has unsaved changes -- a pure derived query, not
    // its own tracked bit: `!UnsavedChangeRanges().empty()`. Was a
    // separately-maintained bool, set unconditionally by any content-
    // changing operation (including undo/redo, even one that landed back
    // on the exact saved content) and cleared only by a save -- a real,
    // user-reported bug found via live testing: Undo() could report "still
    // modified" while the gutter's own unsaved-change indicator correctly
    // showed nothing changed, an inconsistency only possible because the
    // two were two independent signals that could drift apart. Unified
    // onto UnsavedChangeRanges_ so there's exactly one source of truth for
    // "does this buffer differ from what's on disk" -- see
    // UnsavedChangeRanges()'s own doc comment just below.
    [[nodiscard]] bool Modified() const;

    // external-modification-safety follow-up: true when the associated
    // file's on-disk timestamp no longer matches the one recorded at the
    // last load/save (someone else wrote it underneath this buffer), or
    // when a file has appeared underneath a NewFile() buffer that never
    // loaded one. False for a pathless buffer, and false when the file is
    // missing/unstatable -- a deleted file isn't supersession (saving
    // simply recreates it), and auto-revert has nothing to reload from.
    // Stats the file on every call; callers on hot paths shouldn't call
    // this per frame (the save command and the periodic auto-revert sweep
    // are the intended consumers).
    [[nodiscard]] bool ExternallyModified() const;

    // Reloads the associated file from disk, replacing this buffer's
    // content wholesale: recorded as one normal, undoable step; point is
    // clamped into the new content; mark, secondary cursors, narrowing,
    // and fold markers are cleared (all positioned against content that no
    // longer exists). Clears Modified() -- the buffer now matches disk by
    // definition. Throws like FromFile on any read failure (including the
    // file having turned binary), leaving the buffer untouched.
    void Revert();

    // external-modification-round-2 follow-up: unlike Revert() (which
    // discards this buffer's local edits wholesale), three-way merges
    // fresh disk content into them, using SavedSnapshot_ as the diff3 base
    // -- exactly the content both this buffer's local edits and the
    // external disk change diverged from (see Text/ThreeWayMerge.h).
    // Typical caller precondition is the same shape AutoRevertBuffers/
    // AutoMergeBuffers already check (Modified() && ExternallyModified()),
    // but not enforced here -- calling this when the file hasn't actually
    // changed just degrades to a trivial "ours, unchanged" merge. Throws
    // like FromFile/Revert() on any read failure, leaving the buffer
    // untouched. Recorded as one normal, undoable step; mark, secondary
    // cursors, narrowing, and fold markers are cleared the same way
    // Revert() clears them (line numbers/offsets can shift arbitrarily);
    // point lands on the merge's first conflict marker if there is one,
    // else stays where it was (clamped). Unlike Revert(), SavedSnapshot_/
    // DiskTimestamp_ advance to the *freshly read disk content*, not the
    // merged result: the buffer now combines local edits with the external
    // change, so Modified() correctly stays true (there's real unsaved
    // content), while a later ExternallyModified() check correctly goes
    // back to false until the file changes again. Returns the number of
    // genuine conflicts merged in as "<<<<<<<" markers -- 0 is a fully
    // automatic, silent merge.
    [[nodiscard]] std::size_t MergeExternalChanges();

    // Replaces this buffer's content wholesale from a string rather than
    // from disk (backup-and-recovery follow-up: what recover-file /
    // ned/recover-backup restore a snapshot through). Mirrors Revert()'s
    // shape exactly -- one normal, undoable step; point clamped; mark,
    // secondary cursors, narrowing, and fold markers cleared -- except it
    // deliberately does NOT touch SavedSnapshot_/DiskTimestamp_ and marks
    // the whole content as one unsaved range: the buffer reads Modified()
    // afterward, because its content now differs from the file on disk,
    // and the restore only becomes permanent via an explicit save.
    void RestoreContent(std::string_view content);

    // read-only-buffers follow-up: a plain, directly-settable flag (unlike
    // Modified(), not derived from anything) -- for a synthesized,
    // no-file-to-save-to buffer (project-search results, project-replace's
    // preview, project-agenda) that the user should never be able to edit
    // in the first place, and whose Modified() state (BuildResultsBuffer
    // itself inserts the synthesized text, which unavoidably marks it
    // modified) should never trigger the close/quit unsaved-changes prompt
    // -- see BufferView::RequestCloseBuffer/StartInteractiveSession's
    // ConfirmQuit case, both of which now gate on `Modified() &&
    // !ReadOnly()`. Every content-mutating method below throws
    // std::runtime_error if this is set, checked once per call at each
    // method's own entry -- the single enforcement point, rather than
    // duplicating the check in every command that happens to call one of
    // them (self-insert-command, kill-line, yank, ...). Deliberately does
    // NOT guard Undo()/Redo() -- out of scope, no real risk for a buffer
    // nobody is expected to type into in the first place.
    //
    // progressive-huge-file-load follow-up: this used to also read true
    // whenever IsLoading() did, forcing every loading buffer read-only
    // regardless of ReadOnly_ -- that coupling is gone. IsLoading() and
    // ReadOnly() are now fully independent: MarkLoading(bool forceReadOnly)
    // is what decides whether a given load keeps its buffer read-only (the
    // existing 16MB-1GB async tier still does) or leaves it editable (the
    // huge-file progressive tier). See MarkLoading's own doc comment.
    [[nodiscard]] bool ReadOnly() const;
    // disk-space-safety follow-up: reason, when given, is what a thrown
    // "read-only" error message names specifically (see ReadOnlyReason()
    // below) instead of the generic "Buffer is read-only." -- e.g.
    // Buffer::FromHugeFile's own disk-space downgrade. Every call
    // (including plain SetReadOnly(true)/SetReadOnly(false) call sites
    // that predate this) sets ReadOnlyReason_ to exactly what's passed
    // here, nullopt by default -- so turning read-only off, or on for an
    // unrelated reason, always clears a stale reason rather than leaving
    // it to describe a state that's no longer true.
    void SetReadOnly(bool readOnly, std::optional<std::string> reason = std::nullopt);
    // The reason last given to SetReadOnly(true, reason), or nullopt if
    // none was given (including whenever ReadOnly() is false). Exposed so
    // a caller can surface it proactively (e.g. right after opening a
    // buffer that came back forced read-only) rather than only on the
    // first refused edit attempt.
    [[nodiscard]] const std::optional<std::string>& ReadOnlyReason() const;

    // binary-safety-guardrails follow-up: true iff this buffer was opened
    // via an explicit "open anyway?" binary override (allowBinary=true)
    // AND LooksBinary() actually fired for it at open time -- never set
    // just because allowBinary was passed defensively (e.g. --force-binary
    // on a file that isn't actually binary). Set once, at open time
    // (FromFile/FromHugeFile/the progressive huge-load placeholder), never
    // afterward -- a buffer doesn't stop being "confirmed binary content"
    // just because loading finished. Read-only; see BinarySafetyOverride()
    // for the user's own escape hatch.
    [[nodiscard]] bool LikelyBinary() const;

    // The user's explicit "yes, treat this like ordinary text" override for
    // a LikelyBinary() buffer -- toggle-binary-safeguards (Commands.cpp) is
    // the only mutator. Independent of ReadOnly()/SetReadOnly(): this gates
    // a *different* set of behaviors (auto-formatting, forced line-ending
    // conversion, ensure-final-newline-on-save -- see BinarySafeguardsActive()
    // below), not editability itself.
    [[nodiscard]] bool BinarySafetyOverride() const;
    void               SetBinarySafetyOverride(bool overridden);

    // The single predicate every guard site checks: LikelyBinary() with no
    // override yet. Byte-level, content-changing save-time behaviors that
    // are safe/expected for real text but can silently corrupt binary
    // content -- format-on-save (and format-buffer/an LSP formatter),
    // ensure-final-newline-on-save, and forced line-ending conversion
    // (Force CRLF/LF policy, and convert-line-endings itself) -- must all
    // check this and refuse/skip while it's true. A buffer that was merely
    // *opened* via allowBinary but never actually looked binary
    // (LikelyBinary() false) is completely unaffected -- these guards only
    // ever activate for a confirmed "this looked binary, you opened it
    // anyway" buffer, and even then only until the user explicitly
    // overrides.
    [[nodiscard]] bool BinarySafeguardsActive() const;

    // large-file-async-load follow-up: true from the moment BufferList hands
    // out a placeholder for a file being loaded in the background until
    // FinishLoad()/FinishHugeLoad() runs. progressive-huge-file-load
    // follow-up: no longer implies ReadOnly() itself (see that method's own
    // doc comment) -- background-maintenance passes (Backup, AutoMerge,
    // PersistentUndo, AutoRevert, Session, RecentFiles) still gate on this
    // directly and should keep doing so, since Storage_ can still be
    // mutated out from under them by the loader while this is true,
    // independent of whether the buffer is user-editable.
    [[nodiscard]] bool IsLoading() const;

    // The two operations BufferList's async file loader uses to populate a
    // placeholder buffer over time -- neither is a normal edit, so neither
    // goes through InsertAtImpl/UndoTree_/UnsavedChangeRanges_ the way every
    // other mutator here does.
    //
    // ReplaceContentForLoad swaps in a fresh, larger prefix of the file as
    // it's read -- a periodic, cheap-to-produce preview (see
    // Source/UI/AsyncFileLoader.h), not a step anyone should be able to
    // undo back out of. Only touches Rope_ and bumps ContentGeneration_ (so
    // BufferView's existing highlight-cache/repaint invalidation picks it
    // up for free, no new plumbing needed there).
    //
    // FinishLoad is the terminal call once the whole file has been read:
    // sets the final content and, unlike ReplaceContentForLoad, resets
    // UndoTree_ and SavedSnapshot_ against it too -- exactly what
    // FromFile's own constructor call does for a normal synchronous load,
    // so Modified() reads false and undo history starts clean at the
    // loaded content, not at every intermediate preview. Clears
    // IsLoading() (and, with it, the forced ReadOnly() above). detectedEnding
    // (crlf-handling follow-up), when given, sets LineEndingKind() the same
    // way FromFile's own detection does -- AsyncFileLoader normalizes to LF
    // itself before either call (content here, like everywhere else, must
    // already be LF-only) and passes its own DetectLineEnding result through
    // only at FinishLoad, once the whole file has actually been seen.
    void ReplaceContentForLoad(Rope content);
    void FinishLoad(Rope content, std::optional<LineEnding> detectedEnding = std::nullopt);

    // progressive-huge-file-load follow-up: the PieceTable-backed
    // counterparts to the two Rope methods above, used by
    // Source/UI/HugeFileLoader.h instead of AsyncFileLoader for anything
    // over HugeFileThreshold(). Unlike the Rope pair, the buffer stays
    // editable throughout (see MarkLoading/ReadOnly above) -- so unlike
    // ReplaceContentForLoad, AppendHugeLoadChunk below genuinely can race
    // against real user edits landing on Storage_ in between two loader
    // callbacks, and unlike FinishLoad, FinishHugeLoad must NOT reset
    // UndoTree_/SavedSnapshot_ (both are already correctly, incrementally
    // maintained by the time it runs -- see AppendHugeLoadChunk's own doc
    // comment). ReplaceContentForHugeLoad is only ever used once, for the
    // first chunk-group (the moment the placeholder buffer gets real
    // content for the first time); every chunk-group after that goes
    // through AppendHugeLoadChunk instead. Sets Storage_ AND SavedSnapshot_
    // to identical PieceTableStorage clones of content -- the placeholder
    // was an empty RopeStorage until now, and AppendHugeLoadChunk's
    // dynamic_cast to PieceTableStorage needs both already converted before
    // the second chunk-group ever lands.
    void ReplaceContentForHugeLoad(PieceTable content);

    // Splices fragment (a chunk-group's worth of freshly-scanned file
    // content, built off the main thread via PieceTable::FromFileRange --
    // see HugeFileLoader) onto the END of Storage_'s current content via
    // the cheap O(log n) PieceTable::Concatenated. Always correct
    // regardless of what the user has typed in the meantime: this only
    // ever runs on the main thread (inside the loader's EventLoop::Post
    // callback), and nothing can exist in the buffer past "however much
    // has loaded so far" -- an edit can only ever touch the already-loaded
    // prefix, never the splice point itself, so appending at
    // Storage_->ByteLength() at the moment this runs is always the correct
    // place, with no relocation needed for Point_/Mark_/SecondaryCursors_/
    // NarrowedRange_ (none of them can already exceed the pre-append
    // length).
    //
    // Also extends SavedSnapshot_ by the identical fragment (independent of
    // whatever edits sit in Storage_) -- SavedSnapshot_ always tracks "pure
    // disk content, however much has been read so far", exactly the same
    // way it already stays untouched by ordinary edits until a real
    // Save(); growing it here is not an edit, just more of what's already
    // on disk becoming visible.
    //
    // Recorded into UndoTree_ via a coalescing lineage kept deliberately
    // separate from ordinary typing (CanAmendLoadAppend_, not CanAmend_) --
    // consecutive appends collapse into one undo step, consecutive
    // keystrokes collapse into their own, and the two kinds can never merge
    // into each other's step. Does NOT touch UnsavedChangeRanges_ (freshly
    // -read-from-disk content is not an unsaved change).
    //
    // Only ever valid to call after ReplaceContentForHugeLoad has already
    // installed a PieceTableStorage-backed Storage_/SavedSnapshot_ once.
    void AppendHugeLoadChunk(PieceTable fragment);

    // Terminal call once the whole file has been read. Mirrors FromHugeFile's
    // own tail (disk-space downgrade check, LineEnding_ = LF,
    // CaptureDiskTimestamp()) and clears IsLoading()/LoadProgress_ --
    // deliberately does NOT touch UndoTree_ or SavedSnapshot_ (both already
    // correctly reflect everything that happened during the load, including
    // any real edits; FinishLoad's own reset of both would destroy that
    // history and incorrectly mark the buffer as matching disk when it
    // might not).
    void FinishHugeLoad();

    // Called once by BufferList right after constructing a placeholder
    // buffer for an async load -- sets IsLoading() true. forceReadOnly also
    // sets ReadOnly_ true for the duration of the load (the existing
    // 16MB-1GB async tier's behavior, unchanged); the huge-file progressive
    // tier passes false, leaving the buffer editable while it streams in
    // (see ReadOnly()'s own doc comment). FinishLoad/FinishHugeLoad
    // unconditionally clear ReadOnly_ back to false at their own start,
    // before either one's own later legitimate SetReadOnly(true, ...)
    // calls (e.g. the disk-space downgrade) -- safe because a freshly
    // constructed placeholder's ReadOnly_ was always false before this set
    // it, so this exactly reproduces the pre-existing end state. Not meant
    // to be called at any other point (there's no matching public "start
    // loading again" use case), which is why this is a bare setter rather
    // than something exposed as part of a larger state machine.
    void MarkLoading(bool forceReadOnly = true);

    // binary-safety-guardrails follow-up: called once by BufferList right
    // after constructing a placeholder buffer (either load tier), mirroring
    // MarkLoading's own contract -- FromFile/FromHugeFile set their
    // internally-computed LikelyBinary_ directly (they're both members
    // already), so this setter exists only for the placeholder-construction
    // call site outside the class. Not meant to be called at any other
    // point.
    void SetLikelyBinary(bool likelyBinary);

    // Set by AsyncFileLoader at construction, cleared by FinishLoad
    // alongside IsLoading() itself. CurrentLoadProgress returns nullptr
    // when no load is in flight; the pointed-to LoadProgress stays owned
    // here (callers read it in place -- e.g. ModeLine's per-frame
    // percentage -- never retain it).
    void                              SetLoadProgress(std::shared_ptr<LoadProgress> progress);
    [[nodiscard]] const LoadProgress* CurrentLoadProgress() const;

    // Bumped by the exact same set of content-changing operations that can
    // make Modified() true (tree-sitter foundation follow-up) -- unlike
    // Modified(), never reset by a save, so it's a cheap, monotonic "has
    // the content actually changed since I last looked" signal a caller
    // can cache against, rather than a byte-for-byte content comparison.
    // Not meaningful across different Buffer instances or process runs, only as
    // a before/after comparison on the same instance.
    [[nodiscard]] std::size_t ContentGeneration() const;

    [[nodiscard]] std::size_t Point() const;
    void                      SetPoint(std::size_t byteOffset);

    void                                              SetMark(std::size_t byteOffset);
    void                                              ClearMark();
    [[nodiscard]] bool                                HasMark() const;
    [[nodiscard]] std::size_t                         Mark() const;   // precondition: HasMark()
    [[nodiscard]] std::pair<std::size_t, std::size_t> Region() const; // precondition: HasMark()

    // Multiple cursors (multi-cursor phase). The primary cursor stays
    // Point_/Mark_ -- every existing single-cursor code path is untouched
    // by this feature existing -- and secondaries are extra (point, mark)
    // pairs relocated across every edit through the exact same
    // RelocateForInsert/RelocateForDelete primitive Point_/Mark_/
    // NarrowedRange_/FoldMarkers_ already route through (the reuse the
    // generic-code-folding entry explicitly anticipated for this feature).
    // Kept sorted by point, deduplicated against each other and the
    // primary; AddCursorAt snaps to a grapheme boundary the same way
    // SetPoint does and refuses (as a silent no-op) a position an existing
    // cursor already occupies. Undo()/Redo() clear all secondaries -- a
    // deliberate v1 simplification: restoring N cursor positions across a
    // snapshot restore has no obviously-right answer, and collapsing is
    // predictable.
    //
    // RemoveLastAddedCursor (multi-cursor-round-2 follow-up) is the
    // "undo the last add" counterpart to a bare AddCursorAt call -- the only
    // prior recourse for an unwanted cursor was ClearSecondaryCursors()'s
    // collapse-everything. AddedCursorOrder_ is a LIFO stack of every point
    // successfully added (cleared on any ClearSecondaryCursors() call, and
    // relocated across edits the same way SecondaryCursors_ itself is), so
    // repeated calls walk back through additions one at a time -- pressing
    // it N times after adding N cursors returns to the single-cursor state,
    // not just undoing the very last add. A popped entry whose cursor is no
    // longer present (already removed, or NormalizeSecondaryCursors merged
    // it into another) is skipped rather than treated as a match failure.
    struct Cursor {
        std::size_t                point = 0;
        std::optional<std::size_t> mark;
        // Swapped in/out alongside point/mark by ForEachCursor so vertical
        // motion tracks a goal column per cursor -- without this, the first
        // cursor's captured column would leak into every later cursor's
        // own next-line/previous-line move within the same batch.
        std::optional<std::size_t> goalColumn;

        bool operator==(const Cursor&) const = default;
    };
    void                                     AddCursorAt(std::size_t point, std::optional<std::size_t> mark = std::nullopt);
    [[nodiscard]] const std::vector<Cursor>& SecondaryCursors() const;
    [[nodiscard]] bool                       HasSecondaryCursors() const;
    void                                     ClearSecondaryCursors();

    // Removes the most recently added secondary cursor still present, per
    // AddedCursorOrder_ above. Returns false, as a no-op, if there is no
    // tracked addition left to undo.
    bool RemoveLastAddedCursor();

    // Runs operation once per cursor -- first as-is for the primary, then
    // once per secondary with that secondary swapped into Point_/Mark_ (so
    // operation just uses the normal Point()/Mark() API and never knows
    // which cursor it's acting for). The whole run is one undo group (see
    // BeginUndoGroup below), and cursors merge/re-sort afterward.
    // operation must not itself add/remove cursors or call ForEachCursor.
    void ForEachCursor(const std::function<void()>& operation);

    // Undo grouping (multi-cursor phase, but deliberately general): while a
    // group is open (nestable), content-mutating methods skip their own
    // per-call UndoTree_ record/amend; EndUndoGroup records one snapshot
    // for the whole batch if anything actually changed. This is what makes
    // one keystroke applied at N cursors undo as one step, not N.
    void BeginUndoGroup();
    void EndUndoGroup();

    // Narrowing (narrow-to-region/widen follow-up): temporarily restricts
    // where point can go and what BufferView displays to a sub-range of the
    // buffer, Emacs-style. Always whole-line-aligned: start snaps down to
    // its containing line's own start byte offset, end snaps up to the
    // start of the line after the last affected one (or the buffer's own
    // end) -- a deliberate scope simplification. Real Emacs narrows to an
    // exact, possibly mid-line byte range; this codebase's line-oriented
    // BufferView display has no per-line partial-content clipping, and
    // building that would be a substantially bigger lift for a feature
    // whose own stated purpose ("restrict... to e.g. one function") is
    // inherently a whole-lines concept for source code anyway.
    //
    // Deliberately does NOT restrict Size()/ByteLength()/Text() or raw
    // DeleteRange/InsertAt at an arbitrary offset -- those keep operating on
    // the whole buffer regardless, matching this feature's own "not new
    // text-manipulation primitives" scope. What IS restricted: InsertAt/
    // InsertAtPoint/DeleteRange keep the narrowed range itself shifted
    // correctly across edits, the exact same treatment Point_/Mark_ already
    // get (typing at the narrowed range's own boundary has to grow it, not
    // desync from it -- extending a narrowed function is the single most
    // common narrowing workflow there is, not an edge case). Point itself
    // is NOT clamped here -- Point_ is mutated by direct assignment in most
    // of Buffer's own motion methods, not funneled through SetPoint, so
    // BufferView::ClampPointToNarrowing (called once per key event, after
    // whichever handler just ran) is what actually keeps point confined;
    // see its own doc comment for why that's the correct, centralized place
    // for it instead of here.
    void                                              NarrowToRegion(std::size_t start, std::size_t end);
    void                                              Widen();
    [[nodiscard]] bool                                IsNarrowed() const;
    [[nodiscard]] std::pair<std::size_t, std::size_t> NarrowedRange() const; // precondition: IsNarrowed()

    // Inserts at point; point moves to the end of the inserted text. Runs of
    // consecutive InsertAtPoint calls (uninterrupted by any other mutating or
    // cursor-moving call) coalesce into a single undo step.
    void InsertAtPoint(std::string_view text);

    // Backspace / delete-char: remove the grapheme cluster immediately before
    // / at point. No-ops at the start/end of the buffer respectively.
    void DeleteBackwardAtPoint();
    void DeleteForwardAtPoint();

    // General-purpose editing not tied to point; used by commands operating
    // on an explicit range (e.g. kill-region). Returns the removed text.
    std::string DeleteRange(std::size_t byteOffset, std::size_t byteLength);
    void        InsertAt(std::size_t byteOffset, std::string_view text);

    // Appends text at the current end of content, regardless of Point.
    // Requires ReadOnly() already true -- this exists specifically so a
    // buffer can stay genuinely user-uneditable while still accepting
    // internally-generated appends (a live log or similar streaming
    // results buffer); calling it on a writable buffer is a caller bug, not
    // a normal runtime condition, so it throws std::logic_error (distinct
    // from every other mutator's std::runtime_error for "user tried to edit
    // a read-only buffer"). If Point already sat at the buffer's own end
    // (the common "was already looking at the tail") it moves forward with
    // the appended text, same tail-follow behavior `tail -f` gives; if
    // Point was anywhere else (the user scrolled up to read older
    // entries), it is left untouched -- both are just InsertAt's own
    // existing RelocateForInsert behavior, not special-cased here.
    void AppendWhileReadOnly(std::string_view text);

    void MoveForward();  // point -> next grapheme boundary
    void MoveBackward(); // point -> previous grapheme boundary

    // Point -> one word forward/backward, Emacs' forward-word/backward-word:
    // skip any non-word characters, then skip word characters, landing right
    // after/before the word. "Word character" is ASCII alphanumeric plus
    // underscore for now -- a deliberate v1 scope cut, not Unicode-aware.
    void MoveForwardWord();
    void MoveBackwardWord();

    // Point -> the end of the current/next sentence forward, or the
    // start of the current/previous sentence backward -- Emacs'
    // forward-sentence/backward-sentence (M-e/M-a). "Sentence end" is a
    // '.'/'!'/'?' followed by whitespace or buffer end; the landing spot
    // skips that trailing whitespace too, so a run of forward-sentence
    // calls lands on each sentence's first real character in turn. Plain
    // ASCII punctuation/whitespace scan, no locale/abbreviation awareness
    // (e.g. "Mr. Smith" reads as two sentences) -- same deliberate v1
    // scope cut MoveForwardWord/MoveBackwardWord make for word characters.
    void MoveForwardSentence();
    void MoveBackwardSentence();

    // Point -> the same column `count` lines below/above, Emacs-style: a run
    // of consecutive vertical-motion calls (uninterrupted by any other
    // point-moving or editing call) remembers the *original* column as a
    // goal, so passing through a shorter line and back out doesn't lose your
    // place. Clamps to the first/last line rather than no-op-ing outright,
    // so e.g. a page-up near the top of the buffer still moves as far as it
    // can instead of doing nothing.
    //
    // tabWidth (tab-rendering-fix follow-up, default 1) is how many columns a
    // literal tab codepoint should count as when computing/matching the goal
    // column -- 1 preserves the old plain-codepoint-count behavior exactly
    // (a tab counts the same as any other single codepoint), so every
    // existing caller that doesn't pass one is unaffected. Buffer has no
    // dependency on Editor/TabWidth.h -- callers that care about the real
    // configured tab width (BufferView, via Commands.cpp) pass it in
    // explicitly; Buffer itself only ever compares a decoded codepoint
    // against the literal tab value, same as it already does for other
    // specific codepoints (e.g. FromFile's BOM check).
    void MoveDownLines(std::size_t count, std::size_t tabWidth = 1);
    void MoveUpLines(std::size_t count, std::size_t tabWidth = 1);
    void MoveToNextLine(std::size_t tabWidth = 1);     // MoveDownLines(1, tabWidth)
    void MoveToPreviousLine(std::size_t tabWidth = 1); // MoveUpLines(1, tabWidth)

    // Byte offset for `column` *visual* columns into `line` (both 0-indexed),
    // clamping the line to the buffer's last line and the column to that
    // line's actual visual width. A pure query -- doesn't move point. Used by
    // MoveToLine internally, and by callers translating an arbitrary
    // on-screen (line, column) position -- e.g. a mouse click -- into a
    // buffer offset. tabWidth is the same "how many columns does a literal
    // tab count as" parameter MoveDownLines/MoveUpLines take, defaulting to 1
    // (plain codepoint counting, matching this function's original
    // behavior) -- pass the real configured tab width to land on the visual
    // column a tab-containing line actually renders at, not the codepoint
    // count. The landing walk is bounded by kMaxTabAwareColumnScan
    // (Buffer.cpp) the same way VisualColumnForByteOffset is -- `column`
    // isn't always screen-width-small, since MoveToLine can carry over a
    // goal column approximated from a pathologically long *other* line.
    [[nodiscard]] std::size_t ByteOffsetForLineAndColumn(std::size_t line, std::size_t column,
                                                         std::size_t tabWidth = 1) const;

    // The visual column byteOffset renders at, walking from lineStart (which
    // must be a line-start byte offset at or before byteOffset) -- the
    // reverse of ByteOffsetForLineAndColumn's landing walk. Originally a
    // MoveToLine-only implementation detail (capturing its own goal column);
    // public since the rectangle-editing follow-up (Editor/Rectangle.h),
    // which needs the same byte-offset-to-column query to compute a
    // rectangular region's own column bounds from point/mark. tabWidth <= 1
    // takes an O(1) fast path (plain codepoint counting via the rope's
    // cached counts, exactly the original pre-tab-aware behavior); tabWidth
    // > 1 walks codepoint-by-codepoint, bounded by kMaxTabAwareColumnScan
    // (Buffer.cpp) so that capturing the goal column while point sits deep
    // inside a pathologically long single line can't regress into an
    // O(line length) scan -- past that bound it falls back to a plain
    // codepoint-distance approximation for the remainder, matching
    // BufferView::VisualColumn's own precedent of trading exactness for
    // boundedness far past anything a real column position could mean
    // visually anyway.
    [[nodiscard]] std::size_t VisualColumnForByteOffset(std::size_t lineStart, std::size_t byteOffset,
                                                        std::size_t tabWidth) const;

    [[nodiscard]] bool CanUndo() const;
    [[nodiscard]] bool CanRedo() const;
    void               Undo();
    void               Redo();

    // persistent-undo follow-up: hands the whole UndoTree out as a flat,
    // JSON-agnostic node list plus which node is current -- Editor/
    // PersistentUndo.h is what actually serializes this to disk; Buffer/
    // Text/ stay free of any JSON dependency, the same split
    // Text/UndoTree.h's own Serialize()/CurrentNodeId() draw.
    [[nodiscard]] std::vector<UndoTree::SerializedNode> SerializeUndo() const;
    [[nodiscard]] std::size_t                           CurrentUndoNodeId() const;

    // Replaces UndoTree_ wholesale with a tree reconstructed from a prior
    // SerializeUndo()/CurrentUndoNodeId() pair, round-tripped through disk
    // in between -- Editor/PersistentUndo.h's restore path, called
    // immediately after a fresh load once its own content-hash check has
    // already confirmed nodes' currentId entry matches this buffer's
    // just-loaded content. Throws std::runtime_error if it doesn't (a
    // caller-contract violation, not a normal runtime condition -- that
    // hash check is supposed to have already ruled this out) or if nodes
    // itself is malformed (see UndoTree::Deserialize). Point/mark/secondary
    // cursors/fold markers are left exactly as they are; only UndoTree_ and
    // CanAmend_ (a freshly restored tree has no in-flight coalescing run)
    // change.
    void RestoreUndoTree(std::vector<UndoTree::SerializedNode> nodes, std::size_t currentId);

    // A generic, Org-agnostic per-position marker (Org-mode fold/unfold
    // follow-up): Buffer has no idea these represent headline fold state --
    // it just tracks a sparse {byte offset -> one of two marker values}
    // map the same way it already tracks Point_/Mark_/NarrowedRange_,
    // relocating entries across every content-mutating edit via the same
    // RelocateForInsert/RelocateForDelete rule those use. A third,
    // unmarked state ("no entry") is free and is what every offset starts
    // as -- interpreting these two explicit values (and the implicit
    // third) as Org's real 3-state subtree visibility is entirely
    // Source/Editor/Org.h's job, not this class's.
    enum class FoldMarker { Collapsed,
                            ChildrenVisible };

    // marker == nullopt erases any existing marker at byteOffset (back to
    // the implicit "unmarked" state); otherwise sets/overwrites it.
    void                                                   SetFoldMarker(std::size_t byteOffset, std::optional<FoldMarker> marker);
    [[nodiscard]] std::optional<FoldMarker>                FoldMarkerAt(std::size_t byteOffset) const;
    [[nodiscard]] const std::map<std::size_t, FoldMarker>& FoldMarkers() const;

    // Bumped by SetFoldMarker only -- mirrors ContentGeneration()'s own
    // "cheap, monotonic did-it-change signal" shape, but for fold-marker
    // state, which isn't content and so doesn't bump ContentGeneration()
    // itself. Lets a caller (BufferView's hidden-line-range cache) detect
    // "did the fold state change" without re-deriving it from FoldMarkers()
    // on every frame.
    [[nodiscard]] std::size_t FoldGeneration() const;

    // Snippet-expansion follow-up: a live snippet session's tabstop fields,
    // as identity-carrying byte ranges relocated across every content-
    // mutating edit through the same RelocateForInsert/RelocateForDelete
    // primitives every other tracked position rides. Deliberately NOT
    // stored as secondary cursors (those are deduplicated by point and
    // cleared on undo -- either would silently destroy a tabstop's
    // identity/ordinal) and NOT a FoldMarkers_-style map (unique keys lose
    // an entry when a delete collapses two fields together); the closest
    // structural sibling is UnsavedChangeRanges_ (a plain vector of ranges,
    // both endpoints relocated). Buffer has no idea what the ranges mean --
    // interpreting them as tabstops is entirely Editor/Snippet.h's job,
    // the same split FoldMarker has with Org/CodeFold.
    //
    // Gravity is active-aware: the (at most one) active range grows when
    // text is inserted at either of its own edges (typing at the active
    // field's boundary extends the field), while an inactive range excludes
    // a boundary insert (so an insert at the seam between two adjacent
    // fields lands in whichever of the two is active, never both). A range
    // whose content is fully deleted becomes degenerate (start == end) and
    // is kept -- an emptied field is still a navigable, refillable field.
    // Undo()/Redo() clear the whole set (the same v1 simplification
    // secondary cursors make: restoring field positions across a snapshot
    // restore has no obviously-right answer, and the owning session treats
    // "ranges gone" as its cue to end).
    struct SnippetRange {
        std::size_t id;           // stable identity across relocation
        int         tabstopIndex; // 0 = final stop; mirrors share an index
        std::size_t start;        // invariant: start <= end
        std::size_t end;
        bool        active = false;

        bool operator==(const SnippetRange&) const = default;
    };
    void                                           SetSnippetRanges(std::vector<SnippetRange> ranges);
    [[nodiscard]] const std::vector<SnippetRange>& SnippetRanges() const;
    void                                           ClearSnippetRanges();
    // Marks the range with this id active (clearing every other range's
    // flag); unknown id just clears them all.
    void SetActiveSnippetRange(std::size_t id);
    // Explicitly repairs one range's endpoints -- the mirror-sync rewrite
    // (delete a mirror's old content, insert the new) leaves the rewritten
    // range degenerate under its own inactive gravity, so relocation alone
    // can't recapture the fresh text; the session pins it back here.
    // Unknown id is a no-op.
    void UpdateSnippetRange(std::size_t id, std::size_t start, std::size_t end);

    // Editable-multibuffer follow-up: a multibuffer excerpt's own byte range
    // within the *composite* buffer, relocated across every content-
    // mutating edit the same way SnippetRange is. Buffer has no idea these
    // represent stitched-in excerpts -- that's Editor/Multibuffer.h's job,
    // the same split FoldMarker/SnippetRange have with their own owning
    // subsystems. Unlike SnippetRange there's no "active" field: excerpts
    // are always separated by protected chrome (headers/rule lines), so
    // there's no adjacent-field seam to disambiguate -- every range simply
    // grows when text lands at either of its own edges. sourcePath/
    // sourceStartByte/sourceEndByte name where a commit should write this
    // excerpt's current text back to; originalText is the snapshot a commit
    // diffs the current composite text against, captured once at build time
    // (single-source-of-truth, not a merge -- this isn't ThreeWayMerge's
    // problem shape). A range emptied by a full delete becomes degenerate
    // (start == end) and is kept, not dropped -- the correct wgrep
    // semantic: committing a degenerate range writes an empty replacement,
    // i.e. a real deletion in the source. editable false marks an excerpt
    // present for navigation only (jump-to-source) but never edit-accepting;
    // most multibuffers today set every range's editable the same way, but
    // it's per-range rather than buffer-wide since nothing about relocation
    // needs it to be uniform.
    //
    // Undo()/Redo() do NOT clear this set the way SnippetRanges_/
    // SecondaryCursors_ are cleared -- see UpdateExcerptRangesForRestore's
    // own doc comment for why that would be actively wrong here (it would
    // silently drop chrome-protection editability on a buffer whose text is
    // still sitting there mid-edit), not just a missed simplification.
    struct ExcerptRange {
        std::size_t            start; // invariant: start <= end
        std::size_t            end;
        std::filesystem::path  sourcePath;
        std::size_t            sourceStartByte = 0;
        std::size_t            sourceEndByte   = 0;
        bool                   editable        = false;
        std::string            originalText; // commit-time diff baseline, see doc comment above

        bool operator==(const ExcerptRange&) const = default;
    };
    void                                            SetExcerptRanges(std::vector<ExcerptRange> ranges);
    [[nodiscard]] const std::vector<ExcerptRange>&  ExcerptRanges() const;
    void                                            ClearExcerptRanges();
    // Repoints one range's originalText to its current committed text and
    // resets sourceStartByte/sourceEndByte to newSourceStart/newSourceEnd --
    // the commit path's own "this excerpt is now in sync" step, so a
    // re-commit with no further edits is a no-op. Matched by start/end
    // (a range's own composite position, still meaningful immediately after
    // a commit since committing doesn't touch the composite buffer's own
    // text). Unknown start/end is a no-op.
    void MarkExcerptRangeCommitted(std::size_t start, std::size_t end, std::string newOriginalText,
                                    std::size_t newSourceStart, std::size_t newSourceEnd);

    // status-gutter unsaved-change-indicator follow-up: byte ranges touched
    // by an edit since this buffer was last loaded/saved -- sorted, merged,
    // non-overlapping. Relocated across every content-mutating edit the
    // same way FoldMarkers_ already is; cleared (and UnsavedChangeGeneration_
    // bumped) on a successful Save()/SaveToFile(). Also the single source
    // of truth Modified() is now derived from (see that method's own doc
    // comment) -- Modified() used to be tracked independently and could
    // disagree with this.
    //
    // A live edit-tracking signal for ordinary typing/deleting, not a real
    // diff against saved content -- typing a character then deleting it
    // still leaves the line marked touched (good enough for "does this
    // line have edits since disk," not a substitute for a real
    // git-diff-gutter, explicitly deferred -- see ROADMAP.md). Undo()/
    // Redo() are the one exception: since they restore a full prior
    // snapshot rather than replaying one edit, they check directly against
    // the last-saved content (SavedSnapshot_) first and clear this
    // entirely when it matches exactly, rather than only ever tracking
    // incrementally -- see UpdateUnsavedRangesForRestore's own comment.
    [[nodiscard]] const std::vector<std::pair<std::size_t, std::size_t>>& UnsavedChangeRanges() const;
    // Bumped whenever UnsavedChangeRanges() changes -- mirrors
    // FoldGeneration()'s own "cheap, did-it-change" signal shape.
    [[nodiscard]] std::size_t UnsavedChangeGeneration() const;

    // LSP client follow-up: an external tool's report about a byte range in
    // this buffer -- a plain, editor-agnostic data shape (no dependency on
    // Source/Editor/Lsp/ or anything JSON-shaped), the same "structured
    // per-position metadata that happens to live on Buffer" role FoldMarker/
    // NarrowedRange_ already have. Unlike FoldMarkers_, not relocated across
    // edits: an LSP server re-reports its full, current diagnostic set after
    // every textDocument/didChange (see Editor/Lsp/LspManager.h), so
    // SetDiagnostics always *replaces* the set wholesale rather than being
    // incrementally maintained -- there's never a reason to relocate a stale
    // range across an edit when a fresh, correct set is coming right behind
    // it.
    struct Diagnostic {
        std::size_t startByte;
        std::size_t endByte;
        enum class Severity { Error,
                              Warning,
                              Information,
                              Hint } severity;
        // prose-diagnostic-callout follow-up: distinguishes a real language
        // server's diagnostic from the prose/spell/grammar checker's (see
        // Editor/Lsp/LspManager.h's kProseLanguageKey) -- BufferView renders
        // the two differently (Prose gets no code-style underline/inline
        // annotation row; it's a right-side callout brace instead, or
        // nothing at all when there's no room), so this has to survive
        // LspManager's per-source merge into this wholesale-replaced set.
        // Defaulted to Code so every other Diagnostic{...} call site in the
        // codebase (tests included) keeps compiling unchanged.
        enum class Origin { Code,
                            Prose } origin = Origin::Code;
        std::string message;

        [[nodiscard]] bool operator==(const Diagnostic&) const = default;
    };

    void                                         SetDiagnostics(std::vector<Diagnostic> diagnostics);
    [[nodiscard]] const std::vector<Diagnostic>& Diagnostics() const;
    // Bumped by SetDiagnostics only -- mirrors FoldGeneration()/
    // UnsavedChangeGeneration()'s own "cheap, did-it-change" signal shape,
    // so BufferView's gutter cache can detect "did the diagnostic set
    // change" without re-deriving it from Diagnostics() every Paint() call.
    [[nodiscard]] std::size_t DiagnosticsGeneration() const;

  private:
    void ClampCursorsToContent();
    void MoveToLine(std::size_t targetLine, std::size_t tabWidth);

    // Shared body of InsertAt/AppendWhileReadOnly -- both public entry
    // points do their own ReadOnly_ check first (InsertAt throws
    // std::runtime_error for "user tried to edit a read-only buffer",
    // AppendWhileReadOnly throws std::logic_error for the opposite caller
    // mistake), then forward here.
    void InsertAtImpl(std::size_t byteOffset, std::string_view text);

    // disk-space-safety follow-up: "Buffer is read-only." when
    // ReadOnlyReason_ is unset, or that reason appended when it is -- every
    // ReadOnly_ throw site below calls this instead of hardcoding the
    // generic string, so a disk-space-forced buffer's first refused edit
    // says why.
    [[nodiscard]] std::string ReadOnlyErrorMessage() const;

    // The one relocation rule every tracked position in this class follows
    // across an edit -- Point_, Mark_, both ends of NarrowedRange_, and
    // FoldMarkers_' keys -- factored out once FoldMarkers_ was about to
    // become a fourth hand-duplicated copy of logic that was already
    // inlined separately (and inconsistently -- see DeleteBackwardAtPoint/
    // DeleteForwardAtPoint's own use below) in every one of InsertAtPoint/
    // DeleteBackwardAtPoint/DeleteForwardAtPoint/DeleteRange/InsertAt.
    //
    // RelocateForInsert: an offset at or after insertOffset shifts forward
    // by length; anything strictly before is untouched.
    [[nodiscard]] static std::size_t RelocateForInsert(std::size_t offset, std::size_t insertOffset,
                                                       std::size_t length);
    // RelocateForDelete: an offset at/past rangeEnd shifts back by the
    // deleted length; an offset strictly inside [rangeStart, rangeEnd)
    // collapses to rangeStart; anything strictly before rangeStart is
    // untouched.
    [[nodiscard]] static std::size_t RelocateForDelete(std::size_t offset, std::size_t rangeStart,
                                                       std::size_t rangeEnd);

    // FoldMarkers_' keys can't be shifted in place (std::map key mutation
    // is undefined) -- these rebuild the map through the two relocation
    // rules above. A delete that collapses two distinct marker offsets onto
    // the same surviving offset loses one of them (map keys are unique) --
    // an accepted, documented edge case, the same "collapses toward one
    // surviving position" behavior Mark_ itself already has.
    void RelocateFoldMarkersForInsert(std::size_t insertOffset, std::size_t length);
    void RelocateFoldMarkersForDelete(std::size_t rangeStart, std::size_t rangeEnd);

    // status-gutter unsaved-change-indicator follow-up: relocates
    // UnsavedChangeRanges_' existing entries (same two relocation rules
    // above) then merges in the newly-touched span -- MarkUnsavedRangeInserted's
    // is exactly [insertOffset, insertOffset + length); MarkUnsavedRangeDeleted's
    // is a single clamped byte at the collapse point (a delete removes
    // content, so there's no span left to mark, only the position it
    // collapsed to -- one byte is enough for the line it maps to at render
    // time). Both bump UnsavedChangeGeneration_.
    void MarkUnsavedRangeInserted(std::size_t insertOffset, std::size_t length);
    void MarkUnsavedRangeDeleted(std::size_t rangeStart, std::size_t rangeEnd);

    // Multi-cursor phase: SecondaryCursors_'s own leg of the relocation
    // every mutator already gives Point_/Mark_/NarrowedRange_/FoldMarkers_
    // -- called right beside RelocateFoldMarkersForInsert/Delete at each of
    // the five content-mutation sites.
    void RelocateSecondaryCursorsForInsert(std::size_t insertOffset, std::size_t length);
    void RelocateSecondaryCursorsForDelete(std::size_t rangeStart, std::size_t rangeEnd);
    // Snippet-expansion follow-up: SnippetRanges_'s own leg of the same
    // per-field relocation, called beside the two above at each of the five
    // content-mutation sites. The insert half implements the active-aware
    // gravity described at SnippetRange's own doc comment rather than
    // RelocateForInsert's uniform at-or-after rule; the delete half is both
    // endpoints through RelocateForDelete unchanged.
    void RelocateSnippetRangesForInsert(std::size_t insertOffset, std::size_t length);
    void RelocateSnippetRangesForDelete(std::size_t rangeStart, std::size_t rangeEnd);
    // Editable-multibuffer follow-up: ExcerptRanges_' own leg of the same
    // per-field relocation, called beside RelocateSnippetRangesForInsert/
    // Delete at each of the five content-mutation sites. Every range (not
    // just an "active" one -- there's no such concept here, see
    // ExcerptRange's own doc comment) grows on an insert at either of its
    // own edges; the delete half is both endpoints through
    // RelocateForDelete unchanged, kept degenerate rather than dropped.
    void RelocateExcerptRangesForInsert(std::size_t insertOffset, std::size_t length);
    void RelocateExcerptRangesForDelete(std::size_t rangeStart, std::size_t rangeEnd);
    // Editable-multibuffer follow-up: whether offset is a legal insertion
    // point (inside some editable ExcerptRange, edge-inclusive) or
    // [rangeStart, rangeEnd) is a legal deletion span (fully inside one
    // editable ExcerptRange). An empty ExcerptRanges_ always answers true --
    // a buffer that was never marked up with excerpt ranges edits exactly
    // as before, at no cost. See the enforcement call sites' own doc
    // comment (InsertAtPoint et al.) for why an illegal edit is a silent
    // no-op rather than a thrown error.
    [[nodiscard]] bool CanInsertAtExcerpt(std::size_t offset) const;
    [[nodiscard]] bool CanDeleteExcerptRange(std::size_t rangeStart, std::size_t rangeEnd) const;
    // Re-sorts by point and drops duplicates (of each other or of the
    // primary) -- edits can collapse two cursors onto one position, the
    // same "collapses toward one surviving position" behavior Mark_ has.
    void NormalizeSecondaryCursors();
    // The undo-record epilogue shared by every content mutator: inside an
    // open group, just marks the group dirty; outside one, records (or,
    // for a plain insert with canAmend, amends) exactly as each mutator
    // did inline before undo grouping existed. Resets CanAmendLoadAppend_
    // -- see RecordOrAmendLoadAppend's own doc comment for why the two
    // coalescing lineages must never merge into each other.
    void RecordOrAmendUndo(bool canAmend);

    // AppendHugeLoadChunk's own undo-record epilogue -- deliberately NOT
    // RecordOrAmendUndo(true), which would let a background load-append
    // coalesce into whatever undo step a real keystroke just left CanAmend_
    // true for (or vice versa: let a keystroke typed right after an append
    // silently merge into that append's step). Either direction would make
    // a single Undo() remove two semantically-unrelated changes at once.
    // Keyed off its own CanAmendLoadAppend_ instead: consecutive appends
    // coalesce into one step, but always into their own lineage, and this
    // resets CanAmend_ so a keystroke immediately after an append always
    // starts its own fresh step too.
    void RecordOrAmendLoadAppend();

    // Shared by Undo()/Redo(): oldStorage is Storage_'s content just before
    // the restore that already happened by the time this runs -- a clone
    // taken before the swap, never materialized to a string (see
    // ChangedByteRange's own comment for why: a huge buffer can't afford
    // that). See SavedSnapshot_'s own doc comment for why this checks
    // against it first rather than only ever diffing oldStorage/Storage_.
    void UpdateUnsavedRangesForRestore(const ITextStorage& oldStorage);

    // Editable-multibuffer follow-up: Undo()/Redo()'s ExcerptRanges_
    // counterpart to UpdateUnsavedRangesForRestore, called the same way
    // (oldText is Rope_'s content just before the restore that already
    // happened). Deliberately NOT a blanket clear the way ClearSnippetRanges/
    // ClearSecondaryCursors handle Undo()/Redo() -- a snippet session ending
    // is harmless (the buffer's content is correct either way), but an
    // ExcerptRange also gates point-level editability (CanInsertAtExcerpt/
    // CanDeleteExcerptRange): dropping it out from under a buffer whose text
    // is still sitting there mid-edit would either lock the whole composite
    // buffer or, worse, silently open its header/rule chrome to editing.
    // Reuses ChangedByteRange the exact way UpdateUnsavedRangesForRestore
    // does, composing the result onto RelocateExcerptRangesForDelete/Insert
    // instead of MarkUnsavedRangeDeleted/Inserted -- same diff, different
    // tracked field.
    void UpdateExcerptRangesForRestore(const ITextStorage& oldStorage);

    // Re-stats Path_ and records its current timestamp (or clears the
    // record if the file is missing/unstatable) -- called wherever content
    // and disk are brought into agreement: load, save, revert.
    void CaptureDiskTimestamp();

    std::string                          Name_;
    std::optional<std::filesystem::path> Path_;
    // The file's last_write_time as of the last load/save/revert -- what
    // ExternallyModified() compares against. nullopt for a pathless or
    // NewFile() buffer (no on-disk content has ever been seen).
    std::optional<std::filesystem::file_time_type>     DiskTimestamp_;
    // See LineEndingKind()/SetLineEndingOverride's own doc comments above.
    // Defaults to LF, matching a NewFile() buffer's own implicit ending.
    ned::text::LineEnding                              LineEnding_ = ned::text::LineEnding::LF;
    // See Content()'s own doc comment above -- Rope-backed for every
    // ordinary buffer, piece-table-backed only for a huge one. Every
    // internal mutator/query below goes through this, never a bare Rope
    // directly, so the same logic works for either concrete storage.
    std::unique_ptr<ITextStorage>                      Storage_;
    UndoTree                                           UndoTree_;
    std::size_t                                        Point_ = 0;
    std::optional<std::size_t>                         Mark_;
    std::optional<std::pair<std::size_t, std::size_t>> NarrowedRange_;                 // see NarrowToRegion's own doc comment
    std::vector<Cursor>                                SecondaryCursors_;              // see AddCursorAt; sorted, deduplicated
    std::vector<std::size_t>                           AddedCursorOrder_;              // see RemoveLastAddedCursor
    int                                                UndoGroupDepth_        = 0;     // see BeginUndoGroup
    bool                                               UndoGroupDirty_        = false; // any mutation inside the open group?
    bool                                               CursorIterationActive_ = false; // see ForEachCursor + RelocateSecondaryCursorsForDelete
    bool                                               CanAmend_              = false;
    bool                                               CanAmendLoadAppend_    = false; // see AppendHugeLoadChunk's own doc comment above
    bool                                               ReadOnly_              = false; // see ReadOnly()/SetReadOnly()'s own doc comment above
    std::optional<std::string>                        ReadOnlyReason_;                // see SetReadOnly()/ReadOnlyReason()'s own doc comment above
    bool                                               Loading_               = false; // see IsLoading()'s own doc comment above
    std::shared_ptr<LoadProgress>                      LoadProgress_;                  // see SetLoadProgress
    bool                                               LikelyBinary_          = false; // see LikelyBinary()'s own doc comment above
    bool                                               BinarySafetyOverride_  = false; // see BinarySafetyOverride()'s own doc comment above
    // Set by MoveToNextLine/MoveToPreviousLine, cleared by every other
    // point-moving or editing call -- see their doc comment above.
    std::optional<std::size_t>        GoalColumn_;
    std::size_t                       ContentGeneration_ = 0; // see ContentGeneration()
    std::map<std::size_t, FoldMarker> FoldMarkers_;           // see FoldMarker's own doc comment above
    std::size_t                       FoldGeneration_ = 0;    // see FoldGeneration()
    std::vector<SnippetRange>         SnippetRanges_;         // see SnippetRange's own doc comment above
    std::vector<ExcerptRange>         ExcerptRanges_;         // see ExcerptRange's own doc comment above

    std::vector<Diagnostic> Diagnostics_;               // see Diagnostics()'s own doc comment
    std::size_t             DiagnosticsGeneration_ = 0; // see DiagnosticsGeneration()

    std::vector<std::pair<std::size_t, std::size_t>> UnsavedChangeRanges_;         // see UnsavedChangeRanges()'s own doc comment
    std::size_t                                      UnsavedChangeGeneration_ = 0; // see UnsavedChangeGeneration()
    // Content as of the last load/save -- cheap to hold (Rope is
    // structurally shared, not a deep string copy). Undo()/Redo() diff
    // against this after restoring a snapshot to catch "landed back on
    // exactly the saved content" regardless of the path taken to get
    // there, clearing UnsavedChangeRanges_ wholesale in that case rather
    // than leaving a technically-correct-but-misleading residual marker at
    // wherever the undone edit happened to sit.
    std::unique_ptr<ITextStorage> SavedSnapshot_;
};

} // namespace ned::text

#endif // NED_TEXT_BUFFER_H
