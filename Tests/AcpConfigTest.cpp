#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Acp/AcpConfig.h"

using ned::editor::acp::AcpAgentCommand;
using ned::editor::acp::SetAcpAgentCommand;

TEST_CASE("AcpAgentCommand is nullopt for a name nothing was ever configured for", "[Acp]") {
    REQUIRE_FALSE(AcpAgentCommand("an-agent-nobody-configured").has_value());
}

TEST_CASE("SetAcpAgentCommand registers a command retrievable by name", "[Acp]") {
    SetAcpAgentCommand("acp-config-test-agent", {"claude-code-acp"});

    const auto command = AcpAgentCommand("acp-config-test-agent");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"claude-code-acp"});

    SetAcpAgentCommand("acp-config-test-agent", {}); // cleanup -- process-wide state
}

TEST_CASE("Re-registering an agent's command overwrites the previous one", "[Acp]") {
    SetAcpAgentCommand("acp-config-test-overwrite", {"first-agent"});
    SetAcpAgentCommand("acp-config-test-overwrite", {"second-agent"});

    const auto command = AcpAgentCommand("acp-config-test-overwrite");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"second-agent"});

    SetAcpAgentCommand("acp-config-test-overwrite", {}); // cleanup
}

TEST_CASE("An empty argv clears an existing registration", "[Acp]") {
    SetAcpAgentCommand("acp-config-test-clear", {"some-agent"});
    REQUIRE(AcpAgentCommand("acp-config-test-clear").has_value());

    SetAcpAgentCommand("acp-config-test-clear", {});
    REQUIRE_FALSE(AcpAgentCommand("acp-config-test-clear").has_value());
}
