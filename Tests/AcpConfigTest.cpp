#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Acp/Config.h"
#include "Editor/Acp/PanelConfig.h"

using ned::editor::acp::AgentCommand;
using ned::editor::acp::PanelDock;
using ned::editor::acp::PanelSizePercent;
using ned::editor::acp::GetAcpPanelDock;
using ned::editor::acp::SetAcpAgentCommand;
using ned::editor::acp::SetAcpPanelDock;
using ned::editor::acp::SetAcpPanelSizePercent;

TEST_CASE("AgentCommand is nullopt for a name nothing was ever configured for", "[Acp]") {
    REQUIRE_FALSE(AgentCommand("an-agent-nobody-configured").has_value());
}

TEST_CASE("SetAcpAgentCommand registers a command retrievable by name", "[Acp]") {
    SetAcpAgentCommand("acp-config-test-agent", {"claude-code-acp"});

    const auto command = AgentCommand("acp-config-test-agent");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"claude-code-acp"});

    SetAcpAgentCommand("acp-config-test-agent", {}); // cleanup -- process-wide state
}

TEST_CASE("Re-registering an agent's command overwrites the previous one", "[Acp]") {
    SetAcpAgentCommand("acp-config-test-overwrite", {"first-agent"});
    SetAcpAgentCommand("acp-config-test-overwrite", {"second-agent"});

    const auto command = AgentCommand("acp-config-test-overwrite");
    REQUIRE(command.has_value());
    REQUIRE(*command == std::vector<std::string>{"second-agent"});

    SetAcpAgentCommand("acp-config-test-overwrite", {}); // cleanup
}

TEST_CASE("An empty argv clears an existing registration", "[Acp]") {
    SetAcpAgentCommand("acp-config-test-clear", {"some-agent"});
    REQUIRE(AgentCommand("acp-config-test-clear").has_value());

    SetAcpAgentCommand("acp-config-test-clear", {});
    REQUIRE_FALSE(AgentCommand("acp-config-test-clear").has_value());
}

TEST_CASE("PanelDock defaults to Bottom", "[Acp]") {
    REQUIRE(GetAcpPanelDock() == PanelDock::Bottom);
}

TEST_CASE("SetAcpPanelDock/GetAcpPanelDock round-trip", "[Acp]") {
    SetAcpPanelDock("right");
    REQUIRE(GetAcpPanelDock() == PanelDock::Right);

    SetAcpPanelDock("bottom");
    REQUIRE(GetAcpPanelDock() == PanelDock::Bottom); // cleanup -- process-wide state
}

TEST_CASE("SetAcpPanelDock ignores an unrecognized value, leaving the current setting unchanged", "[Acp]") {
    SetAcpPanelDock("right");
    SetAcpPanelDock("sideways");
    REQUIRE(GetAcpPanelDock() == PanelDock::Right);

    SetAcpPanelDock("bottom"); // cleanup
}

TEST_CASE("PanelSizePercent defaults to 30", "[Acp]") {
    REQUIRE(PanelSizePercent() == 30);
}

TEST_CASE("SetAcpPanelSizePercent/PanelSizePercent round-trip and clamp", "[Acp]") {
    SetAcpPanelSizePercent(45);
    REQUIRE(PanelSizePercent() == 45);

    SetAcpPanelSizePercent(5);
    REQUIRE(PanelSizePercent() == 15);

    SetAcpPanelSizePercent(200);
    REQUIRE(PanelSizePercent() == 70);

    SetAcpPanelSizePercent(30); // cleanup
}
