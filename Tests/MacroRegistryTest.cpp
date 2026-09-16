#include <catch2/catch_test_macros.hpp>

#include "Editor/Key.h"
#include "Editor/MacroRegistry.h"

using ned::editor::ClearAllMacros;
using ned::editor::KeyChord;
using ned::editor::MacroForName;
using ned::editor::MacroNames;
using ned::editor::ParseKeySequence;
using ned::editor::RegisterMacro;

namespace {

// The registry is process-wide static state -- every test scopes its
// registrations so nothing leaks across tests (SnippetRegistryTest.cpp's
// own precedent).
struct MacroRegistryGuard {
    MacroRegistryGuard() {
        ClearAllMacros();
    }
    ~MacroRegistryGuard() {
        ClearAllMacros();
    }
};

} // namespace

TEST_CASE("MacroRegistry registers and looks up a macro", "[MacroRegistry]") {
    const MacroRegistryGuard    guard;
    const std::vector<KeyChord> chords = ParseKeySequence("C-x C-s");
    RegisterMacro("save", chords);
    REQUIRE(MacroForName("save") == chords);
    REQUIRE(!MacroForName("missing").has_value());
}

TEST_CASE("MacroRegistry overwrites on re-register and clears on empty sequence", "[MacroRegistry]") {
    const MacroRegistryGuard guard;
    RegisterMacro("go", ParseKeySequence("C-n"));
    RegisterMacro("go", ParseKeySequence("C-p"));
    REQUIRE(MacroForName("go") == ParseKeySequence("C-p"));
    RegisterMacro("go", {});
    REQUIRE(!MacroForName("go").has_value());
}

TEST_CASE("MacroRegistry rejects an empty name", "[MacroRegistry]") {
    const MacroRegistryGuard guard;
    REQUIRE_THROWS_AS(RegisterMacro("", ParseKeySequence("C-n")), std::runtime_error);
}

TEST_CASE("MacroNames returns every registered name sorted", "[MacroRegistry]") {
    const MacroRegistryGuard guard;
    RegisterMacro("zeta", ParseKeySequence("C-n"));
    RegisterMacro("alpha", ParseKeySequence("C-p"));
    REQUIRE(MacroNames() == std::vector<std::string>{"alpha", "zeta"});
}
