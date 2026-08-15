#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include "Editor/Key.h"
#include "UI/KeyTranslation.h"

using ned::editor::SpecialKey;
using ned::ui::TranslateKey;

TEST_CASE("TranslateKey maps C0 control codes to Control+letter", "[KeyTranslation]") {
    const auto ctrlA = TranslateKey(ftxui::Event::CtrlA);
    REQUIRE(ctrlA.has_value());
    REQUIRE(ctrlA->Control);
    REQUIRE(ctrlA->Special == SpecialKey::None);
    REQUIRE(ctrlA->Codepoint == U'a');

    const auto ctrlZ = TranslateKey(ftxui::Event::CtrlZ);
    REQUIRE(ctrlZ.has_value());
    REQUIRE(ctrlZ->Control);
    REQUIRE(ctrlZ->Codepoint == U'z');
}

TEST_CASE("TranslateKey maps named special keys", "[KeyTranslation]") {
    REQUIRE(TranslateKey(ftxui::Event::Tab)->Special == SpecialKey::Tab);
    REQUIRE(TranslateKey(ftxui::Event::Return)->Special == SpecialKey::Enter);
    REQUIRE(TranslateKey(ftxui::Event::Escape)->Special == SpecialKey::Escape);
    REQUIRE(TranslateKey(ftxui::Event::Backspace)->Special == SpecialKey::Backspace);
    REQUIRE(TranslateKey(ftxui::Event::Delete)->Special == SpecialKey::Delete);
    REQUIRE(TranslateKey(ftxui::Event::Home)->Special == SpecialKey::Home);
    REQUIRE(TranslateKey(ftxui::Event::End)->Special == SpecialKey::End);
    REQUIRE(TranslateKey(ftxui::Event::PageUp)->Special == SpecialKey::PageUp);
    REQUIRE(TranslateKey(ftxui::Event::PageDown)->Special == SpecialKey::PageDown);
    REQUIRE(TranslateKey(ftxui::Event::ArrowUp)->Special == SpecialKey::Up);
    REQUIRE(TranslateKey(ftxui::Event::ArrowDown)->Special == SpecialKey::Down);
    REQUIRE(TranslateKey(ftxui::Event::ArrowLeft)->Special == SpecialKey::Left);
    REQUIRE(TranslateKey(ftxui::Event::ArrowRight)->Special == SpecialKey::Right);
    REQUIRE(TranslateKey(ftxui::Event::F5)->Special == SpecialKey::F5);
    REQUIRE(TranslateKey(ftxui::Event::F12)->Special == SpecialKey::F12);
}

// Ctrl+Arrow is a real, additive upgrade over the pre-migration translator
// (TermOx/escape had no equivalent esc::Key cases for it at all) -- FTXUI
// hands these to us as their own pre-parsed named constants, effectively for
// free.
TEST_CASE("TranslateKey maps Ctrl+Arrow keys", "[KeyTranslation]") {
    const auto up = TranslateKey(ftxui::Event::ArrowUpCtrl);
    REQUIRE(up.has_value());
    REQUIRE(up->Control);
    REQUIRE(up->Special == SpecialKey::Up);

    const auto right = TranslateKey(ftxui::Event::ArrowRightCtrl);
    REQUIRE(right.has_value());
    REQUIRE(right->Control);
    REQUIRE(right->Special == SpecialKey::Right);
}

TEST_CASE("TranslateKey maps BackTab to Shift+Tab", "[KeyTranslation]") {
    const auto chord = TranslateKey(ftxui::Event::TabReverse);
    REQUIRE(chord.has_value());
    REQUIRE(chord->Shift);
    REQUIRE(chord->Special == SpecialKey::Tab);
}

TEST_CASE("TranslateKey maps graphic characters to literal codepoints", "[KeyTranslation]") {
    const auto a = TranslateKey(ftxui::Event::Character("a"));
    REQUIRE(a.has_value());
    REQUIRE_FALSE(a->Control);
    REQUIRE(a->Special == SpecialKey::None);
    REQUIRE(a->Codepoint == U'a');

    const auto space = TranslateKey(ftxui::Event::Character(" "));
    REQUIRE(space.has_value());
    REQUIRE(space->Codepoint == U' ');
}

// A real upgrade over the pre-migration translator's Escape-then-key
// fallback (see KeyTranslation.h's own header comment): a fast Alt+<key>
// press arrives as one Event whose input() is ESC followed by the key's own
// bytes, confirmed empirically against FTXUI's raw terminal input parser
// during the migration's pre-work spike.
TEST_CASE("TranslateKey maps Alt/Meta+letter to Meta chords", "[KeyTranslation]") {
    const auto altA = TranslateKey(ftxui::Event::AltA);
    REQUIRE(altA.has_value());
    REQUIRE(altA->Meta);
    REQUIRE_FALSE(altA->Control);
    REQUIRE(altA->Codepoint == U'a');
}

// Ctrl+Alt+<letter> is not one of FTXUI's own named constants for every
// letter's *combined* form in the same way CtrlA/AltA are, but the raw byte
// sequence (ESC followed by the C0 control byte for Ctrl+<letter>) is still
// cleanly distinguishable and decodes through the same Meta-prefix path
// TranslateKey already uses for plain Alt+<key> -- confirmed against a real
// [27, 1] byte sequence (Ctrl+Alt+a) during the migration's pre-work spike.
TEST_CASE("TranslateKey maps Ctrl+Alt+letter via the raw escape-prefixed byte sequence", "[KeyTranslation]") {
    const auto chord = TranslateKey(ftxui::Event::Special({static_cast<char>(0x1B), static_cast<char>(1)}));
    REQUIRE(chord.has_value());
    REQUIRE(chord->Meta);
    REQUIRE(chord->Control);
    REQUIRE(chord->Codepoint == U'a');
}

TEST_CASE("TranslateKey returns nullopt for mouse events", "[KeyTranslation]") {
    REQUIRE_FALSE(TranslateKey(ftxui::Event::Mouse("", ftxui::Mouse{})).has_value());
}

TEST_CASE("TranslateKey returns nullopt for malformed multi-byte sequences", "[KeyTranslation]") {
    // A UTF-8 lead byte announcing a 2-byte sequence, followed by a byte
    // that isn't a valid continuation byte (top two bits must be 10).
    const auto malformed = TranslateKey(ftxui::Event::Special({static_cast<char>(0xC2), 'x'}));
    REQUIRE_FALSE(malformed.has_value());
}

TEST_CASE("TranslateKey returns nullopt for an empty event", "[KeyTranslation]") {
    REQUIRE_FALSE(TranslateKey(ftxui::Event::Special("")).has_value());
}
