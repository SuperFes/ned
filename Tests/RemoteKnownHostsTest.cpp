#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Editor/Remote/KnownHosts.h"
#include "FakeRemoteHost.h"

using ned::editor::remote::HostSources;
using ned::editor::remote::KnownHosts;
using ned::editor::remote::MatchHosts;
using ned::editor::remote::ParseKnownHosts;
using ned::editor::remote::ParseSearchDomains;
using ned::editor::remote::ParseSshConfig;
namespace fs = std::filesystem;
using Names  = std::vector<std::string>;

TEST_CASE("ssh_config gives the hosts it names outright", "[Remote][KnownHosts]") {
    const auto names = ParseSshConfig("# a comment\n"
                                      "Host box other.example.com\n"
                                      "    HostName 10.0.0.1\n"
                                      "Host *.example.com !bad.example.com\n"
                                      "host=lower\n"
                                      "  HOST \"quoted name\" b?x  # trailing\n"
                                      "Match host \"*.corp\"\n"
                                      "UserKnownHostsFile ~/.ssh/extra %d/more none\n"
                                      "GlobalKnownHostsFile /etc/ssh/global %u/skipped\n"
                                      "Include conf.d/*.conf ~/abs.conf\n",
                                      "/home/me");
    REQUIRE(names.hosts == Names{"box", "other.example.com", "lower", "quoted name"});
    REQUIRE(names.knownHostsFiles == std::vector<fs::path>{"/home/me/.ssh/extra", "/home/me/more", "/etc/ssh/global"});
    REQUIRE(names.includes == Names{"conf.d/*.conf", "/home/me/abs.conf"});
}

TEST_CASE("known_hosts gives readable names on the default port", "[Remote][KnownHosts]") {
    const Names hosts = ParseKnownHosts("box.example.com,10.0.0.1 ssh-ed25519 AAAA\n"
                                        "\n"
                                        "# comment\n"
                                        "10.0.0.2 ssh-rsa AAAA\n"
                                        "[alt.example.com]:2222 ssh-ed25519 AAAA\n"
                                        "[std.example.com]:22,[10.0.0.3]:22 ssh-ed25519 AAAA\n"
                                        "|1|c2FsdA==|aGFzaA== ssh-ed25519 AAAA\n"
                                        "@cert-authority *.example.com ssh-ed25519 AAAA\n"
                                        "@revoked gone.example.com ssh-ed25519 AAAA\n"
                                        "@cert-authority ca.example.com ssh-ed25519 AAAA\n"
                                        "fe80::1 ssh-ed25519 AAAA\n");
    REQUIRE(hosts == Names{"box.example.com", "10.0.0.2", "std.example.com", "ca.example.com", "fe80::1"});
}

TEST_CASE("resolv.conf's last search or domain line gives the search domains", "[Remote][KnownHosts]") {
    REQUIRE(ParseSearchDomains("nameserver 1.1.1.1\nsearch a.com b.com. ; comment\n") == Names{"a.com", "b.com"});
    REQUIRE(ParseSearchDomains("search a.com\ndomain c.com d.com\n") == Names{"c.com"});
    REQUIRE(ParseSearchDomains("domain c.com\nsearch e.com\n") == Names{"e.com"});
    REQUIRE(ParseSearchDomains("# search x.com\nsearching y.com\n").empty());
}

TEST_CASE("Known hosts rank the user config, then known_hosts, then the system config", "[Remote][KnownHosts]") {
    const fs::path dir  = ned::tests::FreshRemoteDir();
    const fs::path home = dir / "home";
    fs::create_directories(home / ".ssh" / "conf.d");
    fs::create_directories(dir / "etc");
    std::ofstream(home / ".ssh" / "config") << "Host alias\nInclude conf.d/*.conf\nUserKnownHostsFile ~/.ssh/extra_hosts\n";
    std::ofstream(home / ".ssh" / "conf.d" / "a.conf") << "Host included\nInclude conf.d/b.conf\n";
    std::ofstream(home / ".ssh" / "conf.d" / "b.conf") << "Host nested\n";
    std::ofstream(dir / "etc" / "ssh_config") << "Include sys.d/*.conf\n";
    fs::create_directories(dir / "etc" / "sys.d");
    std::ofstream(dir / "etc" / "sys.d" / "x.conf") << "Host system\n";
    std::ofstream(home / ".ssh" / "known_hosts") << "Alias ssh-ed25519 AAAA\nseen.example.com ssh-ed25519 AAAA\n";
    std::ofstream(home / ".ssh" / "extra_hosts") << "extra.example.com ssh-ed25519 AAAA\n";

    const HostSources sources{.home         = home,
                              .userConfig   = home / ".ssh" / "config",
                              .systemConfig = dir / "etc" / "ssh_config",
                              .knownHosts   = {home / ".ssh" / "known_hosts", dir / "missing"}};
    // b.conf comes in twice, through the glob and through a.conf.
    REQUIRE(KnownHosts(sources) == Names{"alias", "included", "nested", "seen.example.com", "extra.example.com", "system"});

    // An Include that loops stops at ssh's own depth.
    std::ofstream(home / ".ssh" / "conf.d" / "b.conf") << "Host nested\nInclude conf.d/b.conf\n";
    REQUIRE(KnownHosts(sources).size() == 6);
    fs::remove_all(dir);
}

TEST_CASE("Hosts match fuzzily, keeping the given order between equals", "[Remote][KnownHosts]") {
    const Names hosts{"zeta.example.com", "members-dev.iad.example.com", "alpha.example.com", "members.example.com"};
    REQUIRE(MatchHosts(hosts, "") == hosts);
    REQUIRE(MatchHosts(hosts, "members") == Names{"members-dev.iad.example.com", "members.example.com"});
    REQUIRE(MatchHosts(hosts, "mdev").front() == "members-dev.iad.example.com");
    REQUIRE(MatchHosts(hosts, "nowhere").empty());
}
