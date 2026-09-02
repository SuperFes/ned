//
// A minimal, reusable "collect one line of text" primitive for
// minibuffer-style prompts (find-file, switch-to-buffer, M-x, the ACP
// composer, the DAP debug console, ...) -- UI-agnostic, driven by BufferView
// the same way IncrementalSearch/QueryReplace are.
//
// Cursor-position-aware: a real byte offset into Text() (always on a
// codepoint boundary), not just an implicit "end of text" -- InsertChar
// inserts at the cursor rather than always appending, and the Move*/
// DeleteForward family let a caller support Left/Right/Home/End/Delete the
// way any real single-line text field would. SetText (Tab-completion,
// history recall) always places the cursor at the end of the replacement
// text -- there's no sensible cursor position to preserve across a wholesale
// swap.
//
// Deliberately has no notion of "confirm" or "cancel" itself: the caller
// reads Text() when it decides the prompt is done (Enter) and just discards
// the object otherwise (Escape/C-g) -- there's nothing here to cancel.
//

#ifndef NED_EDITOR_MINIBUFFERPROMPT_H
#define NED_EDITOR_MINIBUFFERPROMPT_H

#include <cstddef>
#include <string>

namespace ned::editor {

class MinibufferPrompt {
  public:
    explicit MinibufferPrompt(std::string label);

    void InsertChar(char32_t codepoint); // inserts at the cursor; cursor advances past it
    void DeleteBackward();               // backspace: removes the codepoint before the cursor, if any
    void DeleteForward();                // delete: removes the codepoint at the cursor, if any
    void MoveCursorLeft();
    void MoveCursorRight();
    // Word-wise motion (Ctrl-Left/Right in every other single-line field in
    // this codebase) -- ASCII alnum+underscore word classification, mirroring
    // Buffer::MoveForwardWord/MoveBackwardWord's own deliberately-not-
    // Unicode-aware rule exactly (see MinibufferPrompt.cpp's IsWordByte).
    void MoveCursorWordLeft();
    void MoveCursorWordRight();
    void MoveCursorToStart();       // Home
    void MoveCursorToEnd();         // End
    void SetText(std::string text); // wholesale replace, e.g. Tab-completion; cursor moves to the end

    [[nodiscard]] const std::string& Text() const;
    [[nodiscard]] std::string        StatusText() const; // label + current text, for the echo area

    // Byte offset of the cursor within Text() (not StatusText()).
    [[nodiscard]] std::size_t CursorByteOffset() const;

    // The cursor's column (one per codepoint, matching PaintUtf8Row's own
    // one-column-per-codepoint convention) within StatusText() -- what a
    // caller draws its caret cell at.
    [[nodiscard]] int CursorDisplayColumn() const;

  private:
    std::string label_;
    std::string text_;
    std::size_t cursor_ = 0; // byte offset into text_, always on a codepoint boundary
};

} // namespace ned::editor

#endif // NED_EDITOR_MINIBUFFERPROMPT_H
