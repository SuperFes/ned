// ned-agent: the host side of Docs/ClientServerProtocol.md, started by the
// editor as `ssh host ned-agent --stdio`.

#include <csignal>
#include <cstdio>
#include <exception>
#include <string_view>

#include <unistd.h>

#include "Editor/Host/Agent.h"
#include "Editor/Host/Hello.h"
#include "Editor/Mcp/StdioRelay.h"

int main(int argc, char** argv) {
    // The MCP server an ACP agent on this host is given: a relay to the
    // socket the running agent's mcp/listen opened.
    if (argc == 3 && std::string_view(argv[1]) == "--mcp-relay") {
        return ned::editor::mcp::RunStdioRelay(argv[2], "ned-agent --mcp-relay");
    }
    if (argc != 2 || std::string_view(argv[1]) != "--stdio") {
        std::fprintf(stderr, "usage: ned-agent --stdio | --mcp-relay <socket>\n");
        return 2;
    }
    // A write to an editor that already hung up must fail, not kill us.
    std::signal(SIGPIPE, SIG_IGN);

    try {
        const auto failure = ned::editor::host::RunAgent(STDIN_FILENO, STDOUT_FILENO, ned::editor::host::FileBuildId("/proc/self/exe"));
        if (failure) {
            std::fprintf(stderr, "ned-agent: %s\n", failure->c_str());
            return 1;
        }
        return 0;
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "ned-agent: %s\n", e.what());
        return 1;
    }
}
