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

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Rope.h"
#include "UndoTree.h"

namespace ned::text {

class Buffer {
  public:
    explicit Buffer(std::string name, Rope initialContent = Rope());

    // Throws std::runtime_error if the file can't be opened for reading.
    [[nodiscard]] static Buffer FromFile(const std::filesystem::path& path);

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
    // Editor/FinalNewline.h's own header comment for why. Buffer itself
    // stays unaware of the real, configured, Janet-settable default the way
    // it already does for tabWidth elsewhere -- callers that want that pass
    // editor::EnsureFinalNewline() in explicitly (Commands.cpp's
    // save-buffer does).
    void SaveToFile(const std::filesystem::path& path, bool ensureFinalNewline = true);
    // Writes to the buffer's associated file. Throws std::runtime_error if
    // the buffer has none (i.e. FromFile/SaveToFile were never called).
    void Save(bool ensureFinalNewline = true);

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

    [[nodiscard]] const Rope& Content() const;
    [[nodiscard]] std::string Text() const;
    [[nodiscard]] std::size_t Size() const;

    // True if the buffer has unsaved changes: set by any content-changing
    // operation (inserts, deletes, undo/redo), cleared by a successful save.
    // Undo/redo back to the exact saved content still counts as modified --
    // matching Emacs' own (not VSCode-style content-hash-based) behavior,
    // simpler and consistent with everything else about undo in this codebase.
    [[nodiscard]] bool Modified() const;

    // Bumped by the exact same set of content-changing operations that set
    // Modified() (tree-sitter foundation follow-up) -- unlike Modified(),
    // never reset by a save, so it's a cheap, monotonic "has the content
    // actually changed since I last looked" signal a caller can cache
    // against, rather than a byte-for-byte content comparison. Not
    // meaningful across different Buffer instances or process runs, only as
    // a before/after comparison on the same instance.
    [[nodiscard]] std::size_t ContentGeneration() const;

    [[nodiscard]] std::size_t Point() const;
    void                      SetPoint(std::size_t byteOffset);

    void                                              SetMark(std::size_t byteOffset);
    void                                              ClearMark();
    [[nodiscard]] bool                                HasMark() const;
    [[nodiscard]] std::size_t                         Mark() const;   // precondition: HasMark()
    [[nodiscard]] std::pair<std::size_t, std::size_t> Region() const; // precondition: HasMark()

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

    void MoveForward();  // point -> next grapheme boundary
    void MoveBackward(); // point -> previous grapheme boundary

    // Point -> one word forward/backward, Emacs' forward-word/backward-word:
    // skip any non-word characters, then skip word characters, landing right
    // after/before the word. "Word character" is ASCII alphanumeric plus
    // underscore for now -- a deliberate v1 scope cut, not Unicode-aware.
    void MoveForwardWord();
    void MoveBackwardWord();

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

    // status-gutter unsaved-change-indicator follow-up: byte ranges touched
    // by an edit since this buffer was last loaded/saved -- sorted, merged,
    // non-overlapping. Relocated across every content-mutating edit the
    // same way FoldMarkers_ already is; cleared (and UnsavedChangeGeneration_
    // bumped) on a successful Save()/SaveToFile(). A live edit-tracking
    // signal, not a real diff against saved content -- typing a character
    // then deleting it still leaves the line marked touched. Good enough
    // for "does this line have edits since disk," not a substitute for a
    // real git-diff-gutter (explicitly deferred -- see ROADMAP.md).
    [[nodiscard]] const std::vector<std::pair<std::size_t, std::size_t>>& UnsavedChangeRanges() const;
    // Bumped whenever UnsavedChangeRanges() changes -- mirrors
    // FoldGeneration()'s own "cheap, did-it-change" signal shape.
    [[nodiscard]] std::size_t UnsavedChangeGeneration() const;

  private:
    void ClampCursorsToContent();
    void MoveToLine(std::size_t targetLine, std::size_t tabWidth);

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

    std::string                                        Name_;
    std::optional<std::filesystem::path>               Path_;
    Rope                                               Rope_;
    UndoTree                                           UndoTree_;
    std::size_t                                        Point_ = 0;
    std::optional<std::size_t>                         Mark_;
    std::optional<std::pair<std::size_t, std::size_t>> NarrowedRange_; // see NarrowToRegion's own doc comment
    bool                                               CanAmend_ = false;
    // Set by MoveToNextLine/MoveToPreviousLine, cleared by every other
    // point-moving or editing call -- see their doc comment above.
    std::optional<std::size_t>        GoalColumn_;
    bool                              Modified_          = false;
    std::size_t                       ContentGeneration_ = 0; // see ContentGeneration()
    std::map<std::size_t, FoldMarker> FoldMarkers_;           // see FoldMarker's own doc comment above
    std::size_t                       FoldGeneration_ = 0;    // see FoldGeneration()

    std::vector<std::pair<std::size_t, std::size_t>> UnsavedChangeRanges_;      // see UnsavedChangeRanges()'s own doc comment
    std::size_t                                       UnsavedChangeGeneration_ = 0; // see UnsavedChangeGeneration()
};

} // namespace ned::text

#endif // NED_TEXT_BUFFER_H
