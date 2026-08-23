//
// Rectangle/column editing (kill-rectangle/delete-rectangle/yank-rectangle/
// string-rectangle follow-up): operates on the *same* point/mark region
// mouse-drag selection and copy-to-register already set on a Buffer,
// reinterpreted as a bounding box of (line, column) pairs instead of a
// linear byte span -- no new selection mechanism, matching real Emacs'
// classic rectangle commands (without the newer, optional
// rectangle-mark-mode) exactly. Pure functions over Buffer's already-public
// API (Content(), ByteOffsetForLineAndColumn, VisualColumnForByteOffset,
// DeleteRange, InsertAt, ...) -- Buffer itself gained no new primitives for
// this, the same "UI-agnostic, composes Buffer's public surface" shape
// ProjectSearch.h/ProjectReplace.h already establish.
//
// open-rectangle and clear-rectangle are deliberately not here -- lower
// daily value, rarer even among Emacs power users; a documented v1 scope
// cut, not an oversight. See ROADMAP.md.
//

#ifndef NED_EDITOR_RECTANGLE_H
#define NED_EDITOR_RECTANGLE_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor {

// The rectangle's bounds are computed from point's and mark's own (line,
// column) independently, not from Buffer::Region()'s linear byte-order
// min/max -- point's and mark's *byte* order and their *column* order
// aren't always the same relation (e.g. mark on an earlier, longer line at
// a high column, point on a later, shorter line at a low column), so this
// takes the min/max of each endpoint's own line and column separately.
struct RectangleBounds {
    std::size_t startLine;
    std::size_t endLine;
    std::size_t startColumn;
    std::size_t endColumn;
};

// Precondition: buffer.HasMark(). Callers (BufferView) check this
// themselves and report "No rectangle region selected." otherwise, the same
// convention copy-to-register's own "No region to copy." message already
// established.
[[nodiscard]] RectangleBounds ComputeRectangleBounds(const text::Buffer& buffer, std::size_t tabWidth);

// Real Emacs keeps killed-rectangle entirely separate from the main
// kill-ring (no shared rotation, just the one most-recently-killed
// rectangle) -- matched here with its own tiny type rather than folding
// into text::KillRing.
//
// multi-cursor-rectangle follow-up: Set(lines) is equivalent to
// SetBlocks({lines}) -- Lines() always reads blocks_.front(), the "single
// most-recent rectangle" view every pre-multi-cursor call site still reads
// (and what a multi-cursor yank falls back to on a piece-count mismatch,
// see YankRectangleLines below). Narrower than KillRing/RegisterTable's own
// "join into one blob" mismatch fallback -- column data has no sensible
// joined shape the way killed text lines do, so a mismatch here just
// collapses to today's single-block behavior instead.
class RectangleClipboard {
  public:
    void Set(std::vector<std::string> lines);
    void SetBlocks(std::vector<std::vector<std::string>> blocks);

    [[nodiscard]] const std::vector<std::string>&              Lines() const;
    [[nodiscard]] const std::vector<std::vector<std::string>>& Blocks() const;
    [[nodiscard]] bool                                         Empty() const;

  private:
    std::vector<std::vector<std::string>> blocks_;
};

// Process-wide, mutex-guarded static state, mirroring TabWidth.h/
// ProjectRoot.h's exact pattern -- not constructor-threaded through
// WindowManager/Pane/BufferView the way text::KillRing/RegisterTable are. A
// deliberate choice: a single "last killed rectangle" slot is materially
// simpler than RegisterTable's own named multi-entry map, and this
// ROADMAP entry (unlike Registers') carries no "sits next to KillRing.h"
// instruction of its own -- see Rectangle.cpp/ROADMAP.md for the fuller
// reasoning.
void                                    SetRectangleClipboard(std::vector<std::string> lines);
void                                    SetRectangleClipboardBlocks(std::vector<std::vector<std::string>> blocks);
[[nodiscard]] const RectangleClipboard& GlobalRectangleClipboard();

// Deletes the column range bounds describes, line by line, returning the
// deleted text per line (kill-rectangle saves it via SetRectangleClipboard,
// delete-rectangle discards it) -- shared by KillRectangle/DeleteRectangle/
// StringRectangle below so the per-line deletion loop isn't duplicated
// three times. Recomputes each line's own byte offsets fresh, immediately
// before that line's edit, rather than precomputing all of them up front --
// safe despite earlier lines' edits shifting later lines' absolute byte
// offsets, since line *numbers* never change (a rectangle delete never
// removes a newline). A line shorter than startColumn naturally produces a
// zero-length delete (Buffer::DeleteRange's own existing no-op), no special
// casing needed.
[[nodiscard]] std::vector<std::string> DeleteRectangleLines(text::Buffer& buffer, const RectangleBounds& bounds,
                                                            std::size_t tabWidth);

// Both land point at the rectangle's own top-left corner afterward and
// clear the mark, matching real Emacs. Precondition: buffer.HasMark().
void KillRectangle(text::Buffer& buffer, std::size_t tabWidth);
void DeleteRectangle(text::Buffer& buffer, std::size_t tabWidth);

// Precondition: !GlobalRectangleClipboard().Empty() -- caller reports "No
// rectangle to yank." otherwise. Pads a destination line shorter than
// point's own column with spaces before inserting, so the yanked columns
// stay visually aligned rather than landing wherever the short line's
// actual end happens to be -- real Emacs' own yank-rectangle behavior, not
// an invented simplification. Extends the buffer with a fresh blank line at
// its end if the clipboard has more lines than there are lines left below
// point.
void YankRectangle(text::Buffer& buffer, std::size_t tabWidth);

// multi-cursor-rectangle follow-up: YankRectangle's real logic, taking an
// explicit block of lines instead of always reading
// GlobalRectangleClipboard().Lines() -- what a multi-cursor yank-rectangle
// calls per cursor with that cursor's own block.
// YankRectangle(buffer, tabWidth) is a one-line wrapper around this with
// GlobalRectangleClipboard().Lines().
void YankRectangleLines(text::Buffer& buffer, const std::vector<std::string>& lines, std::size_t tabWidth);

// Replaces the column range with replacement on every line of the region.
// Precondition: buffer.HasMark().
void StringRectangle(text::Buffer& buffer, std::string_view replacement, std::size_t tabWidth);

} // namespace ned::editor

#endif // NED_EDITOR_RECTANGLE_H
