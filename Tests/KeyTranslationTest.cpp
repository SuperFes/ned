#include <catch2/catch_test_macros.hpp>

#include "Editor/Key.h"
#include "UI/KeyTranslation.h"

using ned::editor::SpecialKey;
using ned::ui::TranslateKey;

TEST_CASE("TranslateKey maps C0 control codes to Control+letter", "[KeyTranslation]") {
    const auto ctrlA = TranslateKey(esc::Key::StartOfHeading); // Ctrl+a
    REQUIRE(ctrlA.has_value());
    REQUIRE(ctrlA->Control);
    REQUIRE(ctrlA->Special == SpecialKey::None);
    REQUIRE(ctrlA->Codepoint == U'a');

    const auto ctrlZ = TranslateKey(esc::Key::Substitute); // Ctrl+z
    REQUIRE(ctrlZ.has_value());
    REQUIRE(ctrlZ->Control);
    REQUIRE(ctrlZ->Codepoint == U'z');
}

TEST_CASE("TranslateKey maps named special keys", "[KeyTranslation]") {
    REQUIRE(TranslateKey(esc::Key::Tab)->Special == SpecialKey::Tab);
    REQUIRE(TranslateKey(esc::Key::Enter)->Special == SpecialKey::Enter);
    REQUIRE(TranslateKey(esc::Key::Escape)->Special == SpecialKey::Escape);
    REQUIRE(TranslateKey(esc::Key::Backspace)->Special == SpecialKey::Backspace);
    REQUIRE(TranslateKey(esc::Key::Delete)->Special == SpecialKey::Delete);
    REQUIRE(TranslateKey(esc::Key::Home)->Special == SpecialKey::Home);
    REQUIRE(TranslateKey(esc::Key::End)->Special == SpecialKey::End);
    REQUIRE(TranslateKey(esc::Key::PageUp)->Special == SpecialKey::PageUp);
    REQUIRE(TranslateKey(esc::Key::PageDown)->Special == SpecialKey::PageDown);
    REQUIRE(TranslateKey(esc::Key::ArrowUp)->Special == SpecialKey::Up);
    REQUIRE(TranslateKey(esc::Key::ArrowDown)->Special == SpecialKey::Down);
    REQUIRE(TranslateKey(esc::Key::ArrowLeft)->Special == SpecialKey::Left);
    REQUIRE(TranslateKey(esc::Key::ArrowRight)->Special == SpecialKey::Right);
    REQUIRE(TranslateKey(esc::Key::Function5)->Special == SpecialKey::F5);
    REQUIRE(TranslateKey(esc::Key::Function12)->Special == SpecialKey::F12);
}

TEST_CASE("TranslateKey maps BackTab to Shift+Tab", "[KeyTranslation]") {
    const auto chord = TranslateKey(esc::Key::BackTab);
    REQUIRE(chord.has_value());
    REQUIRE(chord->Shift);
    REQUIRE(chord->Special == SpecialKey::Tab);
}

TEST_CASE("TranslateKey maps graphic characters to literal codepoints", "[KeyTranslation]") {
    const auto a = TranslateKey(esc::Key::a);
    REQUIRE(a.has_value());
    REQUIRE_FALSE(a->Control);
    REQUIRE(a->Special == SpecialKey::None);
    REQUIRE(a->Codepoint == U'a');

    const auto space = TranslateKey(esc::Key::Space);
    REQUIRE(space.has_value());
    REQUIRE(space->Codepoint == U' ');
}

TEST_CASE("TranslateKey returns nullopt for unmapped keys", "[KeyTranslation]") {
    REQUIRE_FALSE(TranslateKey(esc::Key::Null).has_value());
    REQUIRE_FALSE(TranslateKey(esc::Key::FileSeparator).has_value());
    REQUIRE_FALSE(TranslateKey(esc::Key::LCtrl).has_value()); // raw-mode-only
}
