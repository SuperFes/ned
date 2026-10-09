//
// Host names worth offering at a connect prompt, from what ssh and the
// resolver already know: the hosts ssh_config names outright, the hosts in
// known_hosts files, and the resolver's search domains. SshConfig reads
// ssh_config as ssh does for one host, to say where a name leads.
//

#ifndef NED_EDITOR_REMOTE_KNOWNHOSTS_H
#define NED_EDITOR_REMOTE_KNOWNHOSTS_H

#include <filesystem>
#include <optional>
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
// read back and revoked keys don't count. A host on a port other than 22 is
// an ssh URI, ssh://host:port. A line's IP addresses count only when it
// names no host.
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

// Where ssh would take a destination, from ssh_config.
struct SshTarget {
    std::string                        hostName; // HostName, else the destination's host
    std::optional<std::string>         user;
    std::string                        port = "22";
    std::vector<std::filesystem::path> identityFiles;
    std::optional<std::string>         proxyJump;

    [[nodiscard]] bool operator==(const SshTarget&) const = default;
};

// ssh_config's Host and Match blocks, evaluated as ssh evaluates them for
// one destination: a Host line's patterns (*, ?, !negation) matched against
// the host as given, ignoring case; Match all, host and originalhost, any
// other Match criterion counted as not matching; Includes read in place;
// the first value given for a keyword wins, and every IdentityFile counts.
class SshConfig {
  public:
    // The user config, then the system one. Missing files contribute nothing.
    [[nodiscard]] static SshConfig Load(const HostSources& sources);
    // One config's text, its relative Includes resolved under includeBase.
    [[nodiscard]] static SshConfig Parse(std::string_view text, const std::filesystem::path& home, const std::filesystem::path& includeBase = {});

    // destination is [user@]host or an ssh URI; its own user and port win.
    [[nodiscard]] SshTarget Resolve(std::string_view destination) const;
    // Where destination leads, when ssh_config sends it anywhere other than
    // its own name says: [user@]host[:port], then " via " a ProxyJump.
    [[nodiscard]] std::string Describe(std::string_view destination) const;

  private:
    struct Entry {
        enum class Kind { Host,
                          Match,
                          Setting,
                          Enter,
                          Leave };
        Kind                     kind = Kind::Setting;
        std::string              keyword; // lower case
        std::vector<std::string> arguments;
    };

    void Append(std::string_view text, const std::filesystem::path& home, const std::filesystem::path& includeBase, int depth);

    std::vector<Entry> entries_;
};

// The user config's hosts, then known_hosts hosts, then the system
// config's, each once. Missing or unreadable files contribute nothing. A
// known_hosts entry on another port that a config host already leads to is
// left out.
[[nodiscard]] std::vector<std::string> KnownHosts(const HostSources& sources);

// The hosts typed fuzzily matches, best first. Equal matches keep the order
// given, so the caller's ranking of sources survives an empty query.
[[nodiscard]] std::vector<std::string> MatchHosts(const std::vector<std::string>& hosts, std::string_view typed);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_KNOWNHOSTS_H
