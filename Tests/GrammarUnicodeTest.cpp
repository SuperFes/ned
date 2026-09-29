#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "Editor/Grammar/Compile/Unicode.h"

using namespace ned::editor::grammar::compile;

namespace {

CharacterSet PropertyOrEmpty(std::string_view name) {
    return unicode::Property(name).value_or(CharacterSet::Empty());
}

} // namespace

TEST_CASE("A two-letter category holds its characters and no others", "[GrammarUnicode]") {
    const CharacterSet upper = PropertyOrEmpty("Lu");
    REQUIRE(upper.Contains(U'A'));
    REQUIRE(upper.Contains(0x03A9)); // GREEK CAPITAL LETTER OMEGA
    REQUIRE_FALSE(upper.Contains(U'a'));
    REQUIRE_FALSE(upper.Contains(U'1'));
}

TEST_CASE("A one-letter category and its long alias are the union of its two-letter categories", "[GrammarUnicode]") {
    const CharacterSet letters = PropertyOrEmpty("Lu").Add(PropertyOrEmpty("Ll")).Add(PropertyOrEmpty("Lt")).Add(PropertyOrEmpty("Lm")).Add(PropertyOrEmpty("Lo"));
    REQUIRE(PropertyOrEmpty("L") == letters);
    REQUIRE(PropertyOrEmpty("Letter") == letters);
}

TEST_CASE("Unassigned codepoints are Cn and the supplementary private-use planes are Co", "[GrammarUnicode]") {
    const CharacterSet unassigned = PropertyOrEmpty("Cn");
    REQUIRE(unassigned.Contains(0x0378));
    REQUIRE(unassigned.Contains(0xFFFF));
    REQUIRE(PropertyOrEmpty("Co").Contains(0x10FFFD));
}

TEST_CASE("An unknown property name is nullopt", "[GrammarUnicode]") {
    REQUIRE_FALSE(unicode::Property("Not_A_Property").has_value());
}

TEST_CASE("IsAlphabetic accepts letters and letter numbers only", "[GrammarUnicode]") {
    REQUIRE(unicode::IsAlphabetic(U'a'));
    REQUIRE(unicode::IsAlphabetic(0x2160)); // ROMAN NUMERAL ONE (Nl)
    REQUIRE_FALSE(unicode::IsAlphabetic(U'1'));
    REQUIRE_FALSE(unicode::IsAlphabetic(U'_'));
}

TEST_CASE("CaseFoldOrbit joins every character the simple case mappings connect", "[GrammarUnicode]") {
    REQUIRE(unicode::CaseFoldOrbit(U'k') == std::vector<std::uint32_t>{U'K', U'k', 0x212A}); // KELVIN SIGN
    REQUIRE(unicode::CaseFoldOrbit(U'1') == std::vector<std::uint32_t>{U'1'});
}

// The compiled tables have to be byte-identical on every build machine, so
// the Unicode data is the generator's pinned version, never the system's.
// Bumping that version (Tools/gen-unicode-tables.py) is expected to move
// these two.
TEST_CASE("Unicode data is the pinned 16.0.0, whatever the build machine has", "[GrammarUnicode]") {
    const CharacterSet letters = PropertyOrEmpty("L");
    REQUIRE(letters.Contains(0x1C89));       // CYRILLIC CAPITAL LETTER TJE, new in 16.0
    REQUIRE_FALSE(letters.Contains(0x0C5C)); // unassigned until 17.0
}
