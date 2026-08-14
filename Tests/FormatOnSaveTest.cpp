#include <catch2/catch_test_macros.hpp>

#include "Editor/FormatOnSave.h"

using ned::editor::FormatCommand;
using ned::editor::RunFormatCommand;
using ned::editor::SetFormatCommand;

namespace {

// FormatCommand is process-wide state (see FormatOnSave.h's own doc comment
// on why); every test that sets one must leave it unset for the next test,
// guaranteed via RAII rather than a manual reset at the end of the test
// (which a failed REQUIRE partway through would skip).
struct FormatCommandGuard {
    ~FormatCommandGuard() {
        SetFormatCommand(std::nullopt);
    }
};

} // namespace

TEST_CASE("FormatCommand is unset by default", "[FormatOnSave]") {
    const FormatCommandGuard guard;
    REQUIRE_FALSE(FormatCommand().has_value());
}

TEST_CASE("SetFormatCommand/FormatCommand round-trip", "[FormatOnSave]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("cat"));

    REQUIRE(FormatCommand().has_value());
    REQUIRE(*FormatCommand() == "cat");
}

TEST_CASE("RunFormatCommand returns nullopt when no command is configured", "[FormatOnSave]") {
    const FormatCommandGuard guard;
    REQUIRE_FALSE(RunFormatCommand("hello").has_value());
}

TEST_CASE("RunFormatCommand returns the command's stdout on success", "[FormatOnSave]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("tr 'a-z' 'A-Z'"));

    const auto result = RunFormatCommand("hello world");
    REQUIRE(result.has_value());
    REQUIRE(*result == "HELLO WORLD");
}

TEST_CASE("RunFormatCommand round-trips multi-line input through cat unchanged", "[FormatOnSave]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("cat"));

    const auto result = RunFormatCommand("line one\nline two\nline three\n");
    REQUIRE(result.has_value());
    REQUIRE(*result == "line one\nline two\nline three\n");
}

TEST_CASE("RunFormatCommand returns nullopt when the command exits non-zero", "[FormatOnSave]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("false"));

    REQUIRE_FALSE(RunFormatCommand("hello").has_value());
}

TEST_CASE("RunFormatCommand returns nullopt when the command produces no output", "[FormatOnSave]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("true")); // exits 0, writes nothing

    REQUIRE_FALSE(RunFormatCommand("hello").has_value());
}

TEST_CASE("An empty command string is treated the same as unset", "[FormatOnSave]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string(""));

    REQUIRE_FALSE(RunFormatCommand("hello").has_value());
}
