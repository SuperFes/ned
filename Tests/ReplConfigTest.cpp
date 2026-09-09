#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Repl/Config.h"

using ned::editor::repl::Command;
using ned::editor::repl::SetCommand;

TEST_CASE("Command is nullopt for a name nothing was ever configured for", "[Repl]") {
    REQUIRE_FALSE(Command("a-repl-nobody-configured").has_value());
}

TEST_CASE("SetCommand registers a command retrievable by name", "[Repl]") {
    SetCommand("repl-config-test-python", {"python3", "-i"});

    const auto command = Command("repl-config-test-python");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"python3", "-i"});

    SetCommand("repl-config-test-python", {}); // cleanup -- process-wide state
}

TEST_CASE("Re-registering a REPL's command overwrites the previous one", "[Repl]") {
    SetCommand("repl-config-test-overwrite", {"first-command"});
    SetCommand("repl-config-test-overwrite", {"second-command"});

    const auto command = Command("repl-config-test-overwrite");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"second-command"});

    SetCommand("repl-config-test-overwrite", {}); // cleanup
}

TEST_CASE("An empty argv clears an existing registration", "[Repl]") {
    SetCommand("repl-config-test-clear", {"some-command"});
    REQUIRE(Command("repl-config-test-clear").has_value());

    SetCommand("repl-config-test-clear", {});
    REQUIRE_FALSE(Command("repl-config-test-clear").has_value());
}
