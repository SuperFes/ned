//
// ACP MCP tool-server bridge, slice 1. Process-wide opt-out toggle for
// AcpManager::StartSession's mcpServers payload -- default on, mirroring
// AutoRevert.h/TabWidth.h's own mutex-guarded static state pattern.
// Configured from Janet via ned/set-acp-mcp-bridge.
//

#ifndef NED_EDITOR_MCP_BRIDGESETTING_H
#define NED_EDITOR_MCP_BRIDGESETTING_H

namespace ned::editor::mcp {

void               SetAcpMcpBridgeEnabled(bool enabled);
[[nodiscard]] bool AcpMcpBridgeEnabled();

} // namespace ned::editor::mcp

#endif // NED_EDITOR_MCP_BRIDGESETTING_H
