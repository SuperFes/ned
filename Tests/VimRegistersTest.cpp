#include <catch2/catch_test_macros.hpp>

#include "Editor/Vim/VimRegisters.h"

using ned::editor::vim::RegisterEntry;
using ned::editor::vim::RegisterKind;
using ned::editor::vim::VimRegisters;

namespace {
RegisterEntry Char(std::string text) {
    return RegisterEntry{{std::move(text)}, RegisterKind::Char};
}
RegisterEntry Line(std::vector<std::string> lines) {
    return RegisterEntry{std::move(lines), RegisterKind::Line};
}
} // namespace

TEST_CASE("An unset register returns nullopt", "[VimRegisters]") {
    VimRegisters registers;
    REQUIRE_FALSE(registers.Get(U'a').has_value());
    REQUIRE_FALSE(registers.Get(0).has_value());
}

TEST_CASE("An explicit named write also mirrors into unnamed", "[VimRegisters]") {
    VimRegisters registers;
    registers.Store(U'a', Char("hello"), false);

    REQUIRE(registers.Get(U'a')->Joined() == "hello");
    REQUIRE(registers.Get(0)->Joined() == "hello");
}

TEST_CASE("Uppercase name appends to the lowercase register", "[VimRegisters]") {
    VimRegisters registers;
    registers.Store(U'a', Char("foo"), false);
    registers.Store(U'A', Char("bar"), false);

    REQUIRE(registers.Get(U'a')->Joined() == "foobar");
    REQUIRE(registers.Get(0)->Joined() == "foobar");
}

TEST_CASE("Uppercase append onto an unset register behaves like a plain write", "[VimRegisters]") {
    VimRegisters registers;
    registers.Store(U'B', Char("first"), false);

    REQUIRE(registers.Get(U'b')->Joined() == "first");
}

TEST_CASE("Unnamed yank routes through \"0, not the numbered ring", "[VimRegisters]") {
    VimRegisters registers;
    registers.Store(0, Char("yanked"), false);

    REQUIRE(registers.Get(U'0')->Joined() == "yanked");
    REQUIRE_FALSE(registers.Get(U'1').has_value());
    REQUIRE(registers.Get(0)->Joined() == "yanked");
}

TEST_CASE("A small unnamed delete routes through \"-, not the numbered ring", "[VimRegisters]") {
    VimRegisters registers;
    registers.Store(0, Char("x"), true);

    REQUIRE(registers.Get(U'-')->Joined() == "x");
    REQUIRE_FALSE(registers.Get(U'1').has_value());
}

TEST_CASE("A large unnamed delete shifts the numbered ring", "[VimRegisters]") {
    VimRegisters registers;
    registers.Store(0, Line({"one"}), true);
    registers.Store(0, Line({"two"}), true);
    registers.Store(0, Line({"three"}), true);

    REQUIRE(registers.Get(U'1')->Joined() == "three\n");
    REQUIRE(registers.Get(U'2')->Joined() == "two\n");
    REQUIRE(registers.Get(U'3')->Joined() == "one\n");
    REQUIRE(registers.Get(0)->Joined() == "three\n");
}

TEST_CASE("The numbered ring drops the oldest entry past \"9", "[VimRegisters]") {
    VimRegisters registers;
    for (int i = 1; i <= 10; ++i) {
        registers.Store(0, Line({"line" + std::to_string(i)}), true);
    }
    REQUIRE(registers.Get(U'1')->Joined() == "line10\n");
    REQUIRE(registers.Get(U'9')->Joined() == "line2\n"); // line1 dropped off the ring
}

TEST_CASE("The blackhole register discards content and leaves unnamed untouched", "[VimRegisters]") {
    VimRegisters registers;
    registers.Store(0, Char("previous"), false); // sets unnamed via "0
    registers.Store(U'_', Char("gone"), true);

    REQUIRE_FALSE(registers.Get(U'_').has_value());
    REQUIRE(registers.Get(0)->Joined() == "previous");
}

TEST_CASE("System clipboard registers are inert under the test guard's disabled clipboard", "[VimRegisters]") {
    VimRegisters registers;
    registers.Store(U'+', Char("clip"), false); // a no-op write (ClipboardEnabled() is forced false for ned_tests)

    REQUIRE_FALSE(registers.Get(U'+').has_value());
}
