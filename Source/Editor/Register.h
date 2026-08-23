//
// Emacs-style registers (point-to-register/jump-to-register/copy-to-register/
// insert-register follow-up): named single-slot storage for either a point
// (or set of points) or a piece of text (or set of pieces), keyed by an
// arbitrary character. Global, not per-buffer -- the same scope KillRing.h/.cpp
// already has, which is exactly why this file sits beside it. Rectangle
// registers are deliberately not here -- Rectangle/column editing (the
// selection concept a rectangle register would need) doesn't exist yet; see
// ROADMAP.md.
//

#ifndef NED_EDITOR_REGISTER_H
#define NED_EDITOR_REGISTER_H

#include <cstddef>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace ned::editor {

// A point (or set of points) saved in a register: which buffer (by name, not
// a raw Buffer* -- a buffer can be closed after being saved into a register,
// and holding a name to re-resolve via BufferList::Find at jump time instead
// of a live pointer sidesteps that dangling-reference risk entirely, the
// same lesson this codebase has already applied elsewhere, e.g.
// BufferList::FindByPath) and a byte offset within it.
//
// multi-cursor-register follow-up: byteOffsets[0] is the primary cursor's
// point; any further entries are secondary cursors' points, in cursor
// order -- always >= 1 entry. jump-to-register restores byteOffsets[0] as
// point and recreates the rest as secondary cursors via
// Buffer::AddCursorAt.
struct PointRegisterValue {
    std::string              bufferName;
    std::vector<std::size_t> byteOffsets;
};

// Values are one kind or the other, never both -- setting a register to a
// new kind of value silently replaces whatever was there before (re-storing
// is expected, ordinary use, not an error to guard against, matching
// CommandRegistry::Register's own overwrite convention).
class RegisterTable {
  public:
    void SetPoint(char32_t name, std::string bufferName, std::vector<std::size_t> byteOffsets);
    void SetText(char32_t name, std::string text); // equivalent to SetTextPieces(name, {text})
    // multi-cursor-register follow-up: one piece per active cursor, in
    // cursor order -- what a multi-cursor copy-to-register stores.
    // pieces.size() == 1 behaves exactly like SetText(pieces[0]).
    void SetTextPieces(char32_t name, std::vector<std::string> pieces);

    // nullptr if name isn't set at all, or is set to the other kind of value.
    [[nodiscard]] const PointRegisterValue* Point(char32_t name) const;
    // The stored pieces joined by "\n" -- unchanged single-cursor meaning;
    // for a multi-piece entry this is the whole-blob fallback a piece-count
    // mismatch on insert-register falls back to.
    [[nodiscard]] const std::string* Text(char32_t name) const;
    // multi-cursor-register follow-up: the stored entry's individual pieces
    // (size 1 for a plain SetText()) -- what a multi-cursor insert-register
    // compares its live cursor count against to decide per-cursor vs
    // whole-blob distribution.
    [[nodiscard]] const std::vector<std::string>* TextPieces(char32_t name) const;

  private:
    struct TextValue {
        std::vector<std::string> pieces;
        std::string              joined;
    };
    std::map<char32_t, std::variant<PointRegisterValue, TextValue>> registers_;
};

} // namespace ned::editor

#endif // NED_EDITOR_REGISTER_H
