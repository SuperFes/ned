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

// Ctrl+Arrow is a real, additive upgrade over the pre-FTXUI translator
// (TermOx/escape had no equivalent esc::Key cases for it at all) -- both
// FTXUI and now Notcurses hand these to us already decoded (a real
// modifier bit alongside a synthesized arrow key), effectively for free.
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

// FTXUI -> Notcurses migration: was a hand-built raw CSI byte sequence
// ("\x1B[1;2A") -- Notcurses decodes that upstream too, the same as every
// other modifier combination; this now just asserts against an
// already-decoded Shift+Arrow ncinput, same shape as the Ctrl+Arrow case
// above.
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

// FTXUI -> Notcurses migration: was "arrives as one Event whose input() is
// ESC followed by the key's own bytes" (a real upgrade, at the time, over
// the pre-FTXUI translator's own Escape-then-key fallback) -- Notcurses
// goes a step further still and hands Alt/Meta over as a genuine modifier
// bit on an already-decoded ncinput, no ESC-prefix byte-timing heuristic
// left at all, on any terminal Notcurses supports.
TEST_CASE("TranslateKey maps Alt/Meta+letter to Meta chords", "[KeyTranslation]") {
    const auto altA = TranslateKey(ned::ui::test::Alt('a'));
    REQUIRE(altA.has_value());
    REQUIRE(altA->Meta);
    REQUIRE_FALSE(altA->Control);
    REQUIRE(altA->Codepoint == U'a');
}

// FTXUI -> Notcurses migration: was a hand-built raw ESC + C0-control-byte
// sequence -- Notcurses decodes Ctrl+Alt+<letter> as two real modifier bits
// on one ncinput directly, same as every other combination.
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

// FTXUI -> Notcurses migration: the old raw-byte translator's own
// malformed-UTF-8-sequence test has no equivalent left to test at all --
// Notcurses decodes UTF-8 upstream, before TranslateKey ever sees an
// ncinput, so there is no "malformed multi-byte sequence" concept left in
// this layer to return nullopt for.

TEST_CASE("TranslateKey returns nullopt for an empty (all-zero) event", "[KeyTranslation]") {
    REQUIRE_FALSE(TranslateKey(ned::ui::Event(ncinput{})).has_value());
}
