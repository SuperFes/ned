#include <catch2/catch_test_macros.hpp>

#include "Editor/Key.h"
#include "TestEvents.h"
#include "UI/KeyTranslation.h"

using ned::editor::SpecialKey;
using ned::ui::TranslateKey;

TEST_CASE("TranslateKey maps C0 control codes to Control+letter", "[KeyTranslation]") {
    const auto ctrlA = TranslateKey(ned::ui::test::Ctrl('a'));
    REQUIRE(ctrlA.has_value());
    REQUIRE(ctrlA->Control);
    REQUIRE(ctrlA->Special == SpecialKey::None);
    REQUIRE(ctrlA->Codepoint == U'a');

    const auto ctrlZ = TranslateKey(ned::ui::test::Ctrl('z'));
    REQUIRE(ctrlZ.has_value());
    REQUIRE(ctrlZ->Control);
    REQUIRE(ctrlZ->Codepoint == U'z');
}

TEST_CASE("TranslateKey maps named special keys", "[KeyTranslation]") {
    REQUIRE(TranslateKey(ned::ui::test::Tab())->Special == SpecialKey::Tab);
    REQUIRE(TranslateKey(ned::ui::test::Return())->Special == SpecialKey::Enter);
    REQUIRE(TranslateKey(ned::ui::test::Escape())->Special == SpecialKey::Escape);
    REQUIRE(TranslateKey(ned::ui::test::Backspace())->Special == SpecialKey::Backspace);
    REQUIRE(TranslateKey(ned::ui::test::Delete())->Special == SpecialKey::Delete);
    REQUIRE(TranslateKey(ned::ui::test::Home())->Special == SpecialKey::Home);
    REQUIRE(TranslateKey(ned::ui::test::End())->Special == SpecialKey::End);
    REQUIRE(TranslateKey(ned::ui::test::PageUp())->Special == SpecialKey::PageUp);
    REQUIRE(TranslateKey(ned::ui::test::PageDown())->Special == SpecialKey::PageDown);
    REQUIRE(TranslateKey(ned::ui::test::ArrowUp())->Special == SpecialKey::Up);
    REQUIRE(TranslateKey(ned::ui::test::ArrowDown())->Special == SpecialKey::Down);
    REQUIRE(TranslateKey(ned::ui::test::ArrowLeft())->Special == SpecialKey::Left);
    REQUIRE(TranslateKey(ned::ui::test::ArrowRight())->Special == SpecialKey::Right);
    REQUIRE(TranslateKey(ned::ui::test::F(5))->Special == SpecialKey::F5);
    REQUIRE(TranslateKey(ned::ui::test::F(12))->Special == SpecialKey::F12);
}

// Notcurses hands Ctrl+Arrow to us already decoded -- a real modifier bit
// alongside a synthesized arrow key.
TEST_CASE("TranslateKey maps Ctrl+Arrow keys", "[KeyTranslation]") {
    const auto up = TranslateKey(ned::ui::test::ArrowUpCtrl());
    REQUIRE(up.has_value());
    REQUIRE(up->Control);
    REQUIRE(up->Special == SpecialKey::Up);

    const auto right = TranslateKey(ned::ui::test::ArrowRightCtrl());
    REQUIRE(right.has_value());
    REQUIRE(right->Control);
    REQUIRE(right->Special == SpecialKey::Right);
}

// Notcurses decodes Shift+Arrow upstream too, the same as every other
// modifier combination; this asserts against an already-decoded Shift+Arrow
// ncinput, same shape as the Ctrl+Arrow case above.
TEST_CASE("TranslateKey maps Shift+Arrow keys", "[KeyTranslation]") {
    const auto up = TranslateKey(ned::ui::test::ArrowUpShift());
    REQUIRE(up.has_value());
    REQUIRE(up->Shift);
    REQUIRE(up->Special == SpecialKey::Up);

    const auto down = TranslateKey(ned::ui::test::ArrowDownShift());
    REQUIRE(down.has_value());
    REQUIRE(down->Shift);
    REQUIRE(down->Special == SpecialKey::Down);

    const auto left = TranslateKey(ned::ui::test::ArrowLeftShift());
    REQUIRE(left.has_value());
    REQUIRE(left->Shift);
    REQUIRE(left->Special == SpecialKey::Left);

    const auto right = TranslateKey(ned::ui::test::ArrowRightShift());
    REQUIRE(right.has_value());
    REQUIRE(right->Shift);
    REQUIRE(right->Special == SpecialKey::Right);
}

TEST_CASE("TranslateKey maps BackTab to Shift+Tab", "[KeyTranslation]") {
    const auto chord = TranslateKey(ned::ui::test::TabReverse());
    REQUIRE(chord.has_value());
    REQUIRE(chord->Shift);
    REQUIRE(chord->Special == SpecialKey::Tab);
}

TEST_CASE("TranslateKey maps graphic characters to literal codepoints", "[KeyTranslation]") {
    const auto a = TranslateKey(ned::ui::test::Character("a"));
    REQUIRE(a.has_value());
    REQUIRE_FALSE(a->Control);
    REQUIRE(a->Special == SpecialKey::None);
    REQUIRE(a->Codepoint == U'a');

    const auto space = TranslateKey(ned::ui::test::Character(" "));
    REQUIRE(space.has_value());
    REQUIRE(space->Codepoint == U' ');
}

// This modifier-bit shape is what kitty-keyboard-protocol terminals actually
// produce; a legacy terminal's fast ESC-prefixed press arrives differently --
// see the LegacyAlt case just below.
TEST_CASE("TranslateKey maps Alt/Meta+letter to Meta chords", "[KeyTranslation]") {
    const auto altA = TranslateKey(ned::ui::test::Alt('a'));
    REQUIRE(altA.has_value());
    REQUIRE(altA->Meta);
    REQUIRE_FALSE(altA->Control);
    REQUIRE(altA->Codepoint == U'a');
}

// The legacy-terminal shape of the same press (every terminal without the
// kitty keyboard protocol, tmux included): Notcurses merges the fast
// ESC+letter pair into one ncinput but records Alt only in the deprecated
// `alt` bool, never in `modifiers` -- a real upstream v3.0.14 gap,
// confirmed via a live keyprobe after M-x failed to fire on real Alt+x
// presses while this file's kitty-shaped case above passed. TranslateKey
// must honor both shapes; see its own comment at the Meta branch.
TEST_CASE("TranslateKey maps a legacy ESC-prefixed Alt+letter to a Meta chord", "[KeyTranslation]") {
    const auto altX = TranslateKey(ned::ui::test::LegacyAlt('x'));
    REQUIRE(altX.has_value());
    REQUIRE(altX->Meta);
    REQUIRE_FALSE(altX->Control);
    REQUIRE(altX->Codepoint == U'x');
}

// Notcurses decodes Ctrl+Alt+<letter> as two real modifier bits on one
// ncinput directly, same as every other combination.
TEST_CASE("TranslateKey maps Ctrl+Alt+letter", "[KeyTranslation]") {
    const auto chord = TranslateKey(ned::ui::test::CtrlAlt('a'));
    REQUIRE(chord.has_value());
    REQUIRE(chord->Meta);
    REQUIRE(chord->Control);
    REQUIRE(chord->Codepoint == U'a');
}

// Byte 0x1F (US) is what a real terminal actually sends for Ctrl+_, and --
// since terminals don't distinguish Shift on top of a control byte -- for a
// physical Ctrl+/ press too. Decoded as Control+'_' specifically so real
// Emacs' own C-_ undo binding (Commands.cpp) is reachable from a real
// terminal; the alternative, binding undo to a literal Control+'/' KeyChord,
// never fires at all, since no real terminal byte can ever produce one.
// Notcurses' own load_ncinput only auto-uppercases/tags NCKEY_MOD_CTRL for
// id in [1,26] (Ctrl+A..Z) -- 0x1F falls outside that range and arrives
// with no modifier bit set at all, which is exactly why DecodeBaseKey
// (KeyTranslation.cpp) special-cases this id directly rather than relying
// on ncinput_ctrl_p.
TEST_CASE("TranslateKey maps byte 0x1F to Control+underscore", "[KeyTranslation]") {
    const auto chord = TranslateKey(ned::ui::test::Character(static_cast<char32_t>(0x1F)));
    REQUIRE(chord.has_value());
    REQUIRE(chord->Control);
    REQUIRE_FALSE(chord->Meta);
    REQUIRE(chord->Special == SpecialKey::None);
    REQUIRE(chord->Codepoint == U'_');
}

// Ctrl+Space is the set-mark-command (C-SPC) chord. A kitty-protocol or
// modifyOtherKeys terminal reports it as id ' ' + NCKEY_MOD_CTRL directly;
// a legacy terminal sends the raw NUL byte, which the vendored Notcurses is
// patched (CMake/PatchNotcursesNulKey.cmake) to normalize to this exact
// same shape before queueing -- id 0 would collide with notcurses_get's
// "no input" return value and be unrecoverable by EventLoop's drain loop,
// so nothing downstream of Notcurses can ever see the unnormalized form.
TEST_CASE("TranslateKey maps Ctrl+Space to the C-SPC chord", "[KeyTranslation]") {
    const auto chord = TranslateKey(ned::ui::test::Ctrl(' '));
    REQUIRE(chord.has_value());
    REQUIRE(chord->Control);
    REQUIRE_FALSE(chord->Meta);
    REQUIRE(chord->Special == SpecialKey::None);
    REQUIRE(chord->Codepoint == U' ');
    REQUIRE(*chord == ned::editor::ParseKeyChord("C-SPC"));
}

TEST_CASE("TranslateKey returns nullopt for mouse events", "[KeyTranslation]") {
    REQUIRE_FALSE(
        TranslateKey(ned::ui::test::Mouse(0, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed))
            .has_value());
}

TEST_CASE("TranslateKey returns nullopt for an empty (all-zero) event", "[KeyTranslation]") {
    REQUIRE_FALSE(TranslateKey(ned::ui::Event(ncinput{})).has_value());
}
