//
// Emacs-style registers (point-to-register/jump-to-register/copy-to-register/
// insert-register follow-up): named single-slot storage for either a point
// or a piece of text, keyed by an arbitrary character. Global, not
// per-buffer -- the same scope KillRing.h/.cpp already has, which is exactly
// why this file sits beside it. Rectangle registers are deliberately not
// here -- Rectangle/column editing (the selection concept a rectangle
// register would need) doesn't exist yet; see ROADMAP.md.
//

#ifndef NED_EDITOR_REGISTER_H
#define NED_EDITOR_REGISTER_H

#include <cstddef>
#include <map>
#include <string>
#include <variant>

namespace ned::editor {

// A point saved in a register: which buffer (by name, not a raw Buffer* --
// a buffer can be closed after being saved into a register, and holding a
// name to re-resolve via BufferList::Find at jump time instead of a live
// pointer sidesteps that dangling-reference risk entirely, the same lesson
// this codebase has already applied elsewhere, e.g. BufferList::FindByPath)
// and a byte offset within it.
struct PointRegisterValue {
    std::string bufferName;
    std::size_t byteOffset;
};

// Values are one kind or the other, never both -- setting a register to a
// new kind of value silently replaces whatever was there before (re-storing
// is expected, ordinary use, not an error to guard against, matching
// CommandRegistry::Register's own overwrite convention).
class RegisterTable {
  public:
    void SetPoint(char32_t name, std::string bufferName, std::size_t byteOffset);
    void SetText(char32_t name, std::string text);

    // nullptr if name isn't set at all, or is set to the other kind of value.
    [[nodiscard]] const PointRegisterValue* Point(char32_t name) const;
    [[nodiscard]] const std::string*        Text(char32_t name) const;

  private:
    std::map<char32_t, std::variant<PointRegisterValue, std::string>> registers_;
};

} // namespace ned::editor

#endif // NED_EDITOR_REGISTER_H
