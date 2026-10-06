#include "Deploy.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <ranges>

#include <unistd.h>

#include "Editor/DataDir.h"
#include "Editor/Host/Hello.h"
#include "Editor/ProcessTimeouts.h"

namespace ned::editor::remote {

namespace {

    constexpr std::string_view kProbeMarker = "ned-agent-probe-1";

    // Fed to `sh -s` on stdin rather than passed as an argument, so the user's
    // login shell, which may not be POSIX, never parses it.
    constexpr std::string_view kProbeScript = R"(echo ned-agent-probe-1
uname -m
root="${XDG_DATA_HOME:-$HOME/.local/share}/ned/agent"
printf '%s\n' "$root"
if cd "$root" 2>/dev/null; then
    for f in */ned-agent; do
        if [ -x "$f" ]; then printf '%s\n' "${f%/ned-agent}"; fi
    done
fi
exit 0
)";

    // The agent arrives on stdin and lands under a temporary name, so an
    // interrupted upload never leaves a partial ned-agent behind. It's an
    // argument to `sh -c`, so it stays on one line and free of backslashes for
    // the login shell's quoting to pass through untouched.
    constexpr std::string_view kUploadScript =
        R"(t="$1/.ned-agent.$$"; trap 'rm -f "$t"' EXIT; umask 077 && mkdir -p "$1" && cat > "$t" && chmod 700 "$t" && mv -f "$t" "$1/ned-agent")";

    std::string ReadFile(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw DeployError("cannot read " + path.string());
        }
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    std::vector<std::filesystem::path> SearchedDirs(const AgentDirCandidates& candidates) {
        if (candidates.environment) {
            return {*candidates.environment};
        }
        std::vector<std::filesystem::path> dirs;
        if (candidates.executableRelative) {
            dirs.push_back(*candidates.executableRelative);
        }
        dirs.push_back(candidates.installed);
        return dirs;
    }

    std::string FirstLine(std::string_view text) {
        const auto end = text.find('\n');
        return std::string(text.substr(0, end));
    }

} // namespace

AgentDirCandidates DefaultAgentDirCandidates() {
    AgentDirCandidates candidates{.environment = std::nullopt, .executableRelative = std::nullopt, .installed = NED_INSTALL_LIBEXECDIR "/agent"};
    if (const char* env = std::getenv("NED_AGENT_DIR"); env != nullptr && *env != '\0') {
        candidates.environment = env;
    }
    // <prefix>/share/ned beside the executable gives <prefix>.
    if (const auto data = DefaultDataDirCandidates().executableRelative) {
        candidates.executableRelative = data->parent_path().parent_path() / "libexec" / "ned" / "agent";
    }
    return candidates;
}

std::optional<std::filesystem::path> FindLocalAgent(const AgentDirCandidates& candidates, std::string_view arch) {
    for (const std::filesystem::path& dir : SearchedDirs(candidates)) {
        const std::filesystem::path agent = dir / arch / "ned-agent";
        if (::access(agent.c_str(), X_OK) == 0 && std::filesystem::is_regular_file(agent)) {
            return agent;
        }
    }
    return std::nullopt;
}

std::vector<std::string> LocalAgentDirectories(const AgentDirCandidates& candidates) {
    std::vector<std::string> arches;
    for (const std::filesystem::path& dir : SearchedDirs(candidates)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            arches.push_back(entry.path().filename().string());
        }
    }
    std::ranges::sort(arches);
    const auto [first, last] = std::ranges::unique(arches);
    arches.erase(first, last);

    std::vector<std::string> directories;
    for (const std::string& arch : arches) {
        if (const std::optional<std::filesystem::path> agent = FindLocalAgent(candidates, arch)) {
            try {
                directories.push_back(host::FileBuildId(*agent) + "-" + arch);
            }
            catch (const std::runtime_error&) {
                // Unreadable, so it can't be deployed either.
            }
        }
    }
    return directories;
}

RemoteHost ParseProbe(std::string_view output) {
    std::vector<std::string_view> lines;
    for (const auto line : std::views::split(output, '\n')) {
        lines.emplace_back(line.begin(), line.end());
    }
    // A login script's last line may lack its newline, leaving the marker
    // at the end of it.
    const auto marker = std::ranges::find_if(std::views::reverse(lines), [](std::string_view line) { return line.ends_with(kProbeMarker); });
    if (marker == std::views::reverse(lines).end()) {
        throw DeployError("the host's probe printed no marker: " + FirstLine(output));
    }
    const auto at = static_cast<std::size_t>(std::distance(marker, std::views::reverse(lines).end()));
    RemoteHost host;
    if (at + 1 < lines.size()) {
        host.arch      = lines[at];
        host.agentRoot = lines[at + 1];
    }
    if (host.arch.empty() || !host.agentRoot.is_absolute()) {
        throw DeployError("the host's probe printed no architecture or data directory");
    }
    for (std::size_t i = at + 2; i < lines.size(); ++i) {
        if (!lines[i].empty()) {
            host.installed.emplace_back(lines[i]);
        }
    }
    return host;
}

RemoteHost ProbeRemote(const SshMaster& master, const std::stop_token& stop) {
    const SessionResult result = RunSession(master, std::vector<std::string>{"sh", "-s"}, kProbeScript, ProtocolReadStallTimeoutMs(), stop);
    if (result.exitCode != 0) {
        throw DeployError("probing " + master.Destination() + " failed: " + (result.errors.empty() ? "no error output" : FirstLine(result.errors)));
    }
    return ParseProbe(result.output);
}

DeployedAgent EnsureAgent(const SshMaster& master, const AgentDirCandidates& candidates, bool reinstall, const std::stop_token& stop) {
    const RemoteHost                           host  = ProbeRemote(master, stop);
    const std::optional<std::filesystem::path> local = FindLocalAgent(candidates, host.arch);
    if (!local) {
        throw DeployError("no agent for " + host.arch + " to run on " + master.Destination());
    }
    DeployedAgent     agent{.remotePath = {}, .build = host::FileBuildId(*local), .arch = host.arch, .uploaded = false};
    const std::string directory = agent.build + "-" + agent.arch;
    agent.remotePath            = host.agentRoot / directory / "ned-agent";
    if (!reinstall && std::ranges::contains(host.installed, directory)) {
        return agent;
    }

    const std::vector<std::string> argv{"sh", "-c", std::string(kUploadScript), "sh", (host.agentRoot / directory).string()};
    const SessionResult            result = RunSession(master, argv, ReadFile(*local), ProtocolWriteStallTimeoutMs(), stop);
    if (result.exitCode != 0) {
        throw DeployError("installing the agent on " + master.Destination() + " failed: " +
                          (result.errors.empty() ? "no error output" : FirstLine(result.errors)));
    }
    agent.uploaded = true;
    return agent;
}

} // namespace ned::editor::remote
