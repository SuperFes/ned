#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Repl/ReplConfig.h"

using ned::editor::repl::ReplCommand;
using ned::editor::repl::SetReplCommand;

TEST_CASE("ReplCommand is nullopt for a name nothing was ever configured for", "[Repl]") {
    REQUIRE_FALSE(ReplCommand("a-repl-nobody-configured").has_value());
}

TEST_CASE("SetReplCommand registers a command retrievable by name", "[Repl]") {
    SetReplCommand("repl-config-test-python", {"python3", "-i"});

    const auto command = ReplCommand("repl-config-test-python");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"python3", "-i"});

    SetReplCommand("repl-config-test-python", {}); // cleanup -- process-wide state
}

TEST_CASE("Re-registering a REPL's command overwrites the previous one", "[Repl]") {
    SetReplCommand("repl-config-test-overwrite", {"first-command"});
    SetReplCommand("repl-config-test-overwrite", {"second-command"});

    const auto command = ReplCommand("repl-config-test-overwrite");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"second-command"});

    SetReplCommand("repl-config-test-overwrite", {}); // cleanup
}

TEST_CASE("An empty argv clears an existing registration", "[Repl]") {
    SetReplCommand("repl-config-test-clear", {"some-command"});
    REQUIRE(ReplCommand("repl-config-test-clear").has_value());

    SetReplCommand("repl-config-test-clear", {});
    REQUIRE_FALSE(ReplCommand("repl-config-test-clear").has_value());
}
