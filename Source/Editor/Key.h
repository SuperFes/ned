//
// A single keystroke, and Emacs `kbd`-style textual parsing for it. This is
// the vocabulary Keymap is built on; the textual parser exists so keybinding
// tables (and, later, Janet-facing define-key calls) can be written as
// "C-x C-s" rather than hand-built structs.
//

#ifndef NED_EDITOR_KEY_H
#define NED_EDITOR_KEY_H

#include <compare>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ned::editor {

enum class SpecialKey {
    None, // not special; KeyChord::Codepoint holds a literal character
    Enter,
    Tab,
    Backspace,
    Delete,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
};

// One keystroke: modifiers plus either a named/special key or a literal
// Unicode codepoint (meaningful only when Special == SpecialKey::None).
// Totally ordered (via defaulted <=>) so it can key a std::map.
struct KeyChord {
    bool       Control   = false;
    bool       Meta      = false; // Alt / Emacs "Meta"
    bool       Shift     = false;
    SpecialKey Special   = SpecialKey::None;
    char32_t   Codepoint = 0;

    auto operator<=>(const KeyChord&) const = default;
    bool operator==(const KeyChord&) const  = default;
};

// Parses Emacs `kbd`-style notation for a single chord: stacked "C-"/"M-"/"S-"
// modifier prefixes, then a named key (RET, TAB, DEL, ESC, SPC, UP/DOWN/LEFT/
// RIGHT, HOME, END, PRIOR/PAGEUP, NEXT/PAGEDOWN, F1-F12) or exactly one
// literal codepoint (e.g. "a", "$", a multi-byte UTF-8 character). Throws
// std::invalid_argument for anything else (empty, unknown name, more than one
// codepoint).
[[nodiscard]] KeyChord ParseKeyChord(std::string_view token);

// Parses a whitespace-separated chord sequence, e.g. "C-x C-s".
[[nodiscard]] std::vector<KeyChord> ParseKeySequence(std::string_view text);

} // namespace ned::editor

#endif // NED_EDITOR_KEY_H
