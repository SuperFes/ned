//
// Host names worth offering at a connect prompt, from what ssh and the
// resolver already know: the hosts ssh_config names outright, the hosts in
// known_hosts files, and the resolver's search domains.
//

#ifndef NED_EDITOR_REMOTE_KNOWNHOSTS_H
#define NED_EDITOR_REMOTE_KNOWNHOSTS_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor::remote {

// What one ssh_config file contributes.
struct SshConfigNames {
    // Host names, in file order. A pattern (*, ?, !) names no one host.
    std::vector<std::string> hosts;
    // UserKnownHostsFile and GlobalKnownHostsFile values, ~ expanded.
    std::vector<std::filesystem::path> knownHostsFiles;
    // Include arguments, ~ expanded, possibly globs, possibly relative.
    std::vector<std::string> includes;
};

[[nodiscard]] SshConfigNames ParseSshConfig(std::string_view text, const std::filesystem::path& home);

// Host names in a known_hosts file, in file order. Hashed entries can't be
// read back, revoked keys don't count, and a host on a port other than 22
// is left out: a destination has no way to say the port. A line's IP
// addresses count only when it names no host.
[[nodiscard]] std::vector<std::string> ParseKnownHosts(std::string_view text);

// The search domains in a resolv.conf; the last search or domain line wins,
// as with the resolver.
[[nodiscard]] std::vector<std::string> ParseSearchDomains(std::string_view text);

struct HostSources {
    std::filesystem::path              home;
    std::filesystem::path              userConfig;   // relative Includes resolve under home/.ssh
    std::filesystem::path              systemConfig; // relative Includes resolve beside it
    std::vector<std::filesystem::path> knownHosts;   // read along with any a config names
};

// ~/.ssh/config, /etc/ssh/ssh_config, and the default known_hosts files.
[[nodiscard]] HostSources DefaultHostSources();

// The user config's hosts, then known_hosts hosts, then the system
// config's, each once. Missing or unreadable files contribute nothing.
[[nodiscard]] std::vector<std::string> KnownHosts(const HostSources& sources);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_KNOWNHOSTS_H
