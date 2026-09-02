//
// Test-only factories for ned::ui::Event. Production code never constructs
// an Event ad hoc -- EventLoop is the only real source of one, built from a
// genuine ncinput -- so these factories live here, in Tests/, rather than
// on ned::ui::Event itself.
//

#ifndef NED_TESTS_TESTEVENTS_H
#define NED_TESTS_TESTEVENTS_H

#include <string_view>

#include "UI/Widget.h"

namespace ned::ui::test {

// A plain keypress of one Unicode codepoint, no modifiers.
[[nodiscard]] Event Character(char32_t codepoint);
[[nodiscard]] Event Character(char ch);
// Decodes the leading (and, for a well-formed single-grapheme literal, only)
// UTF-8 codepoint in utf8 -- covers a literal wider than one ASCII byte.
[[nodiscard]] Event Character(std::string_view utf8);

// Ctrl+<letter> -- letter must be lowercase ('a'-'z').
[[nodiscard]] Event Ctrl(char letter);
// Ctrl+Space -- jump-back's C-x C-SPC binding, the one non-letter Ctrl chord
// this codebase's default keymap uses.
[[nodiscard]] Event CtrlSpace();

// Alt/Meta+<letter>.
[[nodiscard]] Event Alt(char letter);
// The legacy-terminal shape of the same press (fast ESC-prefixed letter):
// deprecated `alt` bool set, `modifiers` empty -- the shape Notcurses
// actually delivers outside the kitty keyboard protocol; see
// TestEvents.cpp's own comment.
[[nodiscard]] Event LegacyAlt(char letter);
// Ctrl+Alt+<letter> -- both modifier bits set at once.
[[nodiscard]] Event CtrlAlt(char letter);

[[nodiscard]] Event Return();
[[nodiscard]] Event Escape();
[[nodiscard]] Event Tab();
[[nodiscard]] Event TabReverse();
[[nodiscard]] Event Backspace();
[[nodiscard]] Event Delete();
[[nodiscard]] Event Home();
[[nodiscard]] Event End();
[[nodiscard]] Event PageUp();
[[nodiscard]] Event PageDown();
[[nodiscard]] Event ArrowLeft();
[[nodiscard]] Event ArrowRight();
[[nodiscard]] Event ArrowUp();
[[nodiscard]] Event ArrowDown();
[[nodiscard]] Event ArrowLeftCtrl();
[[nodiscard]] Event ArrowRightCtrl();
[[nodiscard]] Event ArrowUpCtrl();
[[nodiscard]] Event ArrowDownCtrl();
[[nodiscard]] Event ArrowUpShift();
[[nodiscard]] Event ArrowDownShift();
[[nodiscard]] Event ArrowLeftShift();
[[nodiscard]] Event ArrowRightShift();
[[nodiscard]] Event F(int n); // 1-12

// A mouse event at absolute (screen-space) column/row x/y.
[[nodiscard]] Event Mouse(int x, int y, MouseEvent::Button button, MouseEvent::Motion motion, bool shift = false,
                          bool meta = false, bool control = false);

} // namespace ned::ui::test

#endif // NED_TESTS_TESTEVENTS_H
