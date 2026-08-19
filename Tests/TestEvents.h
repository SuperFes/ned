//
// FTXUI -> Notcurses migration: test-only factories for ned::ui::Event,
// mirroring the small subset of ftxui::Event's own named static factories
// (Event::Character, Event::CtrlX, Event::Return, ...) every UI test file
// used to construct fixtures with. Production code never constructs an
// Event ad hoc -- EventLoop is the only real source of one, built from a
// genuine ncinput -- so these factories live here, in Tests/, rather than
// on ned::ui::Event itself.
//

#ifndef NED_TESTS_TESTEVENTS_H
#define NED_TESTS_TESTEVENTS_H

#include <string_view>

#include "UI/Widget.h"

namespace ned::ui::test {

// A plain keypress of one Unicode codepoint, no modifiers -- mirrors
// ftxui::Event::Character(string), which most call sites used with a
// single ASCII byte.
[[nodiscard]] Event Character(char32_t codepoint);
[[nodiscard]] Event Character(char ch);
// Decodes the leading (and, for a well-formed single-grapheme literal, only)
// UTF-8 codepoint in utf8 -- covers every ftxui::Event::Character("...")
// call site that passed a literal wider than one ASCII byte.
[[nodiscard]] Event Character(std::string_view utf8);

// Ctrl+<letter> -- letter must be lowercase ('a'-'z'); mirrors the whole
// family of named ftxui::Event::CtrlX/CtrlC/CtrlS/... constants with one
// parameterized factory instead of one function per letter.
[[nodiscard]] Event Ctrl(char letter);

// Alt/Meta+<letter> -- mirrors ftxui::Event::AltX and friends.
[[nodiscard]] Event Alt(char letter);
// Ctrl+Alt+<letter> -- both modifier bits set at once, the decoded-input
// equivalent of the old raw-byte translator's ESC-then-C0-byte sequence.
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

// A mouse event at absolute (screen-space) column/row x/y -- mirrors the
// shape every ftxui::Event::Mouse(...)-constructing test fixture built by
// hand via a raw ftxui::Mouse struct (there was no single named factory
// under FTXUI either).
[[nodiscard]] Event Mouse(int x, int y, MouseEvent::Button button, MouseEvent::Motion motion, bool shift = false,
                          bool meta = false, bool control = false);

} // namespace ned::ui::test

#endif // NED_TESTS_TESTEVENTS_H
