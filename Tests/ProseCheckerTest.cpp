#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Lsp/ProseChecker.h"

using ned::editor::lsp::ProseCheckerCommand;
using ned::editor::lsp::ProseCheckingEnabled;
using ned::editor::lsp::SetProseCheckerCommand;
using ned::editor::lsp::SetProseCheckingEnabled;

namespace {

    // ProseCheckerTestGuard.cpp forces ProseCheckingEnabled() false for the
    // whole ned_tests binary (so no test anywhere accidentally spawns a real
    // harper-ls just because it happens to be on the machine's $PATH) --
    // every test here that flips it back on must restore that steady state
    // before returning, even on an assertion failure. RAII rather than a
    // manual cleanup line at the end of each TEST_CASE, matching the same
    // reasoning REQUIRE-throws-on-failure gives everywhere else in this
    // codebase's tests.
    struct RestoreProseCheckingDisabled {
        ~RestoreProseCheckingDisabled() {
            SetProseCheckerCommand({});
            SetProseCheckingEnabled(false);
        }
    };

} // namespace

TEST_CASE("SetProseCheckingEnabled/ProseCheckingEnabled round-trip", "[Lsp][ProseChecker]") {
    const RestoreProseCheckingDisabled restore;

    SetProseCheckingEnabled(true);
    REQUIRE(ProseCheckingEnabled());

    SetProseCheckingEnabled(false);
    REQUIRE_FALSE(ProseCheckingEnabled());
}

TEST_CASE("ProseCheckingEnabled(false) forces ProseCheckerCommand to nullopt regardless of any override", "[Lsp][ProseChecker]") {
    const RestoreProseCheckingDisabled restore;

    SetProseCheckingEnabled(false);
    SetProseCheckerCommand({"some-checker", "--stdio"});

    REQUIRE_FALSE(ProseCheckerCommand().has_value());
}

TEST_CASE("SetProseCheckerCommand registers an override ProseCheckerCommand returns verbatim", "[Lsp][ProseChecker]") {
    const RestoreProseCheckingDisabled restore;

    SetProseCheckingEnabled(true);
    SetProseCheckerCommand({"ltex-ls"});

    const auto command = ProseCheckerCommand();
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"ltex-ls"});
}

TEST_CASE("An empty argv clears an explicit override rather than disabling the checker", "[Lsp][ProseChecker]") {
    const RestoreProseCheckingDisabled restore;

    SetProseCheckingEnabled(true);
    SetProseCheckerCommand({"a-made-up-checker-nobody-has-installed"});
    REQUIRE(ProseCheckerCommand() == std::vector<std::string>{"a-made-up-checker-nobody-has-installed"});

    SetProseCheckerCommand({}); // clears the override -- falls through to auto-detection, not to disabled
    // Whatever auto-detection resolves to (harper-ls found, or nullopt),
    // it's never the cleared override -- the only assertion that's true
    // regardless of what's on the machine running this test.
    REQUIRE(ProseCheckerCommand() != std::vector<std::string>{"a-made-up-checker-nobody-has-installed"});
}
