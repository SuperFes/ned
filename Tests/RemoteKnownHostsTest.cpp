#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Editor/PostQueue.h"
#include "Editor/Remote/HostLookup.h"
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

TEST_CASE("A host lookup reports the names that resolve, in order", "[Remote][KnownHosts]") {
    const fs::path                  dir = ned::tests::FreshRemoteDir();
    ned::editor::PostQueue          queue;
    ned::editor::remote::HostLookup lookup(queue, ned::tests::FakeResolver(dir));
    std::optional<Names>            found;
    const auto                      collect = [&](Names names) { found = std::move(names); };

    lookup.Lookup({"good.a", "bad.a", "good.b"}, collect);
    REQUIRE(ned::tests::PumpUntil(queue, [&] { return found.has_value(); }));
    REQUIRE(*found == Names{"good.a", "good.b"});

    // Each search domain, except one the name already ends in.
    std::ofstream(dir / "resolv.conf") << "search a.test b.test\n";
    found.reset();
    lookup.Qualify("good", collect);
    REQUIRE(ned::tests::PumpUntil(queue, [&] { return found.has_value(); }));
    REQUIRE(*found == Names{"good.a.test", "good.b.test"});
    found.reset();
    lookup.Qualify("good.a.test", collect);
    REQUIRE(ned::tests::PumpUntil(queue, [&] { return found.has_value(); }));
    REQUIRE(*found == Names{"good.a.test.b.test"});

    // A replaced lookup never answers.
    bool replaced = false;
    lookup.Lookup({"good.first"}, [&](Names) { replaced = true; });
    found.reset();
    lookup.Lookup({"good.second"}, collect);
    REQUIRE(ned::tests::PumpUntil(queue, [&] { return found.has_value(); }));
    REQUIRE(*found == Names{"good.second"});
    REQUIRE_FALSE(replaced);
    fs::remove_all(dir);
}

TEST_CASE("A host lookup that hangs is cut off, and cancelling kills it", "[Remote][KnownHosts]") {
    const fs::path                         dir = ned::tests::FreshRemoteDir();
    ned::editor::PostQueue                 queue;
    ned::editor::remote::HostLookupOptions options = ned::tests::FakeResolver(dir);
    options.limit                                  = std::chrono::milliseconds(200);
    ned::editor::remote::HostLookup lookup(queue, options);

    std::optional<Names> found;
    lookup.Lookup({"slow.a", "good.a"}, [&](Names names) { found = std::move(names); });
    REQUIRE(ned::tests::PumpUntil(queue, [&] { return found.has_value(); }, std::chrono::seconds(2)));
    REQUIRE(*found == Names{"good.a"});

    options.limit = std::chrono::seconds(30);
    ned::editor::remote::HostLookup patient(queue, options);
    bool                            answered = false;
    patient.Lookup({"slow.b"}, [&](Names) { answered = true; });
    REQUIRE_FALSE(ned::tests::PumpUntil(queue, [&] { return answered; }, std::chrono::milliseconds(200)));
    const auto started = std::chrono::steady_clock::now();
    patient.Cancel();
    REQUIRE(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
    REQUIRE_FALSE(ned::tests::PumpUntil(queue, [&] { return answered; }, std::chrono::milliseconds(100)));
    fs::remove_all(dir);
}
