//
// An MCP stdio server that is only a relay: the command an ACP agent is
// given as ned's MCP server (`ned --mcp-stdio-relay`, or `ned-agent
// --mcp-relay` on a host). It connects to the Unix socket the bridge
// listens on, then moves bytes stdin -> socket and socket -> stdout until
// either side ends, parsing nothing: one thread per direction, since the
// process does nothing else.
//

#ifndef NED_EDITOR_MCP_STDIORELAY_H
#define NED_EDITOR_MCP_STDIORELAY_H

#include <string>
#include <string_view>

namespace ned::editor::mcp {

// The process's exit status; label prefixes what it says on stderr.
[[nodiscard]] int RunStdioRelay(const std::string& socketPath, std::string_view label);

} // namespace ned::editor::mcp

#endif // NED_EDITOR_MCP_STDIORELAY_H
