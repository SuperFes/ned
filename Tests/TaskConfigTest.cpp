#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Tasks/TaskConfig.h"

using ned::editor::tasks::SetTaskCommand;
using ned::editor::tasks::TaskCommand;

TEST_CASE("TaskCommand is nullopt for a name nothing was ever configured for", "[Tasks]") {
    REQUIRE_FALSE(TaskCommand("a-task-nobody-configured").has_value());
}

TEST_CASE("SetTaskCommand registers a command retrievable by name", "[Tasks]") {
    SetTaskCommand("task-config-test-build", {"cmake", "--build", "."});

    const auto command = TaskCommand("task-config-test-build");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"cmake", "--build", "."});

    SetTaskCommand("task-config-test-build", {}); // cleanup -- process-wide state
}

TEST_CASE("Re-registering a task's command overwrites the previous one", "[Tasks]") {
    SetTaskCommand("task-config-test-overwrite", {"first-command"});
    SetTaskCommand("task-config-test-overwrite", {"second-command"});

    const auto command = TaskCommand("task-config-test-overwrite");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"second-command"});

    SetTaskCommand("task-config-test-overwrite", {}); // cleanup
}

TEST_CASE("An empty argv clears an existing registration", "[Tasks]") {
    SetTaskCommand("task-config-test-clear", {"some-command"});
    REQUIRE(TaskCommand("task-config-test-clear").has_value());

    SetTaskCommand("task-config-test-clear", {});
    REQUIRE_FALSE(TaskCommand("task-config-test-clear").has_value());
}
