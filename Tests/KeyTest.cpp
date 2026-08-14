#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "Editor/Key.h"

using ned::editor::KeyChord;
using ned::editor::ParseKeyChord;
using ned::editor::ParseKeySequence;
using ned::editor::SpecialKey;

TEST_CASE("ParseKeyChord parses a plain literal character", "[Key]") {
    const KeyChord chord = ParseKeyChord("a");

    REQUIRE_FALSE(chord.Control);
    REQUIRE_FALSE(chord.Meta);
    REQUIRE_FALSE(chord.Shift);
    REQUIRE(chord.Special == SpecialKey::None);
    REQUIRE(chord.Codepoint == U'a');
}

TEST_CASE("ParseKeyChord parses single and stacked modifiers", "[Key]") {
    REQUIRE(ParseKeyChord("C-x").Control);
    REQUIRE(ParseKeyChord("M-x").Meta);
    REQUIRE(ParseKeyChord("S-x").Shift);

    const KeyChord stacked = ParseKeyChord("C-M-x");
    REQUIRE(stacked.Control);
    REQUIRE(stacked.Meta);
    REQUIRE(stacked.Codepoint == U'x');
}

TEST_CASE("ParseKeyChord parses named keys", "[Key]") {
    REQUIRE(ParseKeyChord("RET").Special == SpecialKey::Enter);
    REQUIRE(ParseKeyChord("TAB").Special == SpecialKey::Tab);
    REQUIRE(ParseKeyChord("DEL").Special == SpecialKey::Backspace);
    REQUIRE(ParseKeyChord("ESC").Special == SpecialKey::Escape);
    REQUIRE(ParseKeyChord("F5").Special == SpecialKey::F5);

    const KeyChord ctrlEnter = ParseKeyChord("C-RET");
    REQUIRE(ctrlEnter.Control);
    REQUIRE(ctrlEnter.Special == SpecialKey::Enter);
}

TEST_CASE("ParseKeyChord treats SPC as the literal space codepoint", "[Key]") {
    const KeyChord spc = ParseKeyChord("SPC");
    REQUIRE(spc.Special == SpecialKey::None);
    REQUIRE(spc.Codepoint == U' ');
}

TEST_CASE("ParseKeyChord decodes a multi-byte UTF-8 literal", "[Key]") {
    const KeyChord accented = ParseKeyChord("\xC3\xA9"); // 'é'
    REQUIRE(accented.Codepoint == static_cast<char32_t>(0x00E9));
}

TEST_CASE("ParseKeyChord rejects malformed tokens", "[Key]") {
    REQUIRE_THROWS_AS(ParseKeyChord(""), std::invalid_argument);
    REQUIRE_THROWS_AS(ParseKeyChord("NOTAKEY"), std::invalid_argument);
    REQUIRE_THROWS_AS(ParseKeyChord("ab"), std::invalid_argument); // more than one codepoint
}

TEST_CASE("ParseKeySequence splits on whitespace", "[Key]") {
    const auto sequence = ParseKeySequence("C-x C-s");

    REQUIRE(sequence.size() == 2);
    REQUIRE(sequence[0].Control);
    REQUIRE(sequence[0].Codepoint == U'x');
    REQUIRE(sequence[1].Control);
    REQUIRE(sequence[1].Codepoint == U's');
}

TEST_CASE("KeyChord equality and ordering work for use as a map key", "[Key]") {
    const KeyChord a = ParseKeyChord("C-x");
    const KeyChord b = ParseKeyChord("C-x");
    const KeyChord c = ParseKeyChord("C-y");

    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
    REQUIRE((a < c || c < a)); // totally ordered, whichever direction
}
