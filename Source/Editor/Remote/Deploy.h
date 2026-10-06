//
// Getting the right ned-agent onto a remote host (Docs/ClientServerProtocol.md,
// section 4). One probe session reads the host's architecture and the agents
// already installed under $XDG_DATA_HOME/ned/agent/<build>-<arch>/; an agent
// whose build isn't there goes up in one more session through the same
// master. The local agents are static builds kept per architecture under
// libexec/ned/agent/<arch>/, so the remote needs no toolchain, and a missing
// one is a visible error rather than anything the editor build requires.
//

#ifndef NED_EDITOR_REMOTE_DEPLOY_H
#define NED_EDITOR_REMOTE_DEPLOY_H

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Remote/Ssh.h"

namespace ned::editor::remote {

class DeployError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Where local agents are looked for, each holding <arch>/ned-agent. Mirrors
// DataDir.h: $NED_AGENT_DIR is authoritative when set; otherwise
// <executable>/../libexec/ned/agent, then the configured install location.
struct AgentDirCandidates {
    std::optional<std::filesystem::path> environment;
    std::optional<std::filesystem::path> executableRelative;
    std::filesystem::path                installed;
};

[[nodiscard]] AgentDirCandidates DefaultAgentDirCandidates();

[[nodiscard]] std::optional<std::filesystem::path> FindLocalAgent(const AgentDirCandidates& candidates, std::string_view arch);

// What the probe learned about a host.
struct RemoteHost {
    std::string           arch;
    std::filesystem::path agentRoot;
    // The <build>-<arch> directories holding an executable agent.
    std::vector<std::string> installed;
};

// Parses the probe's output. Anything a login script prints ahead of it is
// skipped. Throws DeployError when the output isn't a probe's.
[[nodiscard]] RemoteHost ParseProbe(std::string_view output);

[[nodiscard]] RemoteHost ProbeRemote(const SshMaster& master, const std::stop_token& stop = {});

struct DeployedAgent {
    std::filesystem::path remotePath;
    std::string           build;
    std::string           arch;
    bool                  uploaded = false;
};

// Makes sure the host has the agent this editor would deploy for its
// architecture, uploading it when it's missing or when reinstall is set
// (a hello that reported another build). Throws DeployError when there's
// no local agent for the host's architecture or the install fails, and
// SshError when the connection does.
[[nodiscard]] DeployedAgent EnsureAgent(const SshMaster& master, const AgentDirCandidates& candidates, bool reinstall = false,
                                        const std::stop_token& stop = {});

} // namespace ned::editor::remote

#endif
