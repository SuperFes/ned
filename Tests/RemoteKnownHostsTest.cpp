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
using ned::editor::remote::SshConfig;
using ned::editor::remote::SshTarget;
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

TEST_CASE("known_hosts gives readable names, a host on another port as an ssh URI", "[Remote][KnownHosts]") {
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
                                        "fe80::1 ssh-ed25519 AAAA\n"
                                        "[fe80::2]:2200 ssh-ed25519 AAAA\n");
    REQUIRE(hosts == Names{"box.example.com", "10.0.0.2", "ssh://alt.example.com:2222", "std.example.com", "ca.example.com", "fe80::1",
                           "ssh://[fe80::2]:2200"});
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

TEST_CASE("ssh_config resolves a destination as ssh does: first value wins, patterns and negation", "[Remote][KnownHosts]") {
    const SshConfig config = SshConfig::Parse("Port 2200\n"
                                              "Host box\n"
                                              "    HostName box.example.com\n"
                                              "    User aaron\n"
                                              "    IdentityFile ~/.ssh/box\n"
                                              "Host *.example.com !bad.example.com\n"
                                              "    User fallback\n"
                                              "    ProxyJump gate\n"
                                              "Host short?\n"
                                              "    HostName %h.lan\n"
                                              "Match host \"*.corp\" !host secret.corp\n"
                                              "    User corp\n"
                                              "Match exec \"true\"\n"
                                              "    User never\n"
                                              "Host *\n"
                                              "    IdentityFile ~/.ssh/id_ed25519\n"
                                              "    User everyone\n",
                                              "/home/me");
    CHECK(config.Resolve("box") == SshTarget{.hostName      = "box.example.com",
                                             .user          = "aaron",
                                             .port          = "2200",
                                             .identityFiles = {"~/.ssh/box", "~/.ssh/id_ed25519"},
                                             .proxyJump     = std::nullopt});
    // Matched against the host as given, never the HostName it became.
    CHECK(config.Resolve("box").proxyJump == std::nullopt);
    CHECK(config.Resolve("web.example.com").user == "fallback");
    CHECK(config.Resolve("web.example.com").proxyJump == "gate");
    CHECK(config.Resolve("bad.example.com").user == "everyone");
    CHECK(config.Resolve("BOX").hostName == "box.example.com");
    CHECK(config.Resolve("short1").hostName == "short1.lan");
    CHECK(config.Resolve("dev.corp").user == "corp");
    CHECK(config.Resolve("secret.corp").user == "everyone");
    // The destination's own user and port win.
    CHECK(config.Resolve("root@box").user == "root");
    CHECK(config.Resolve("ssh://box:2222").port == "2222");
    CHECK(config.Resolve("ssh://[fe80::1]:2222").hostName == "fe80::1");

    CHECK(config.Describe("box") == "aaron@box.example.com:2200");
    CHECK(config.Describe("web.example.com") == "fallback@web.example.com:2200 via gate");
    CHECK(SshConfig::Parse("Host plain\n  Port 22\n", "/home/me").Describe("plain").empty());
    CHECK(SshConfig::Parse("", "/home/me").Describe("me@there").empty());
}

TEST_CASE("ssh_config reads an Include in place, applying it only where its block does", "[Remote][KnownHosts]") {
    const fs::path dir = ned::tests::FreshRemoteDir();
    std::ofstream(dir / "work.conf") << "User worker\nHost *\n  Port 2022\n";
    const SshConfig config = SshConfig::Parse("Host work-*\n  Include work.conf\n  HostName %h.example.com\n"
                                              "Host *\n  User me\n",
                                              "/home/me", dir);
    CHECK(config.Resolve("work-1") == SshTarget{.hostName = "work-1.example.com", .user = "worker", .port = "2022", .identityFiles = {}, .proxyJump = {}});
    // The included file's Host * never matches outside its block.
    CHECK(config.Resolve("home") == SshTarget{.hostName = "home", .user = "me", .port = "22", .identityFiles = {}, .proxyJump = {}});
    fs::remove_all(dir);
}

TEST_CASE("Known hosts leave out an ssh URI a config host already reaches", "[Remote][KnownHosts]") {
    const fs::path dir  = ned::tests::FreshRemoteDir();
    const fs::path home = dir / "home";
    fs::create_directories(home / ".ssh");
    std::ofstream(home / ".ssh" / "config") << "Host box\n  HostName box.example.com\n  Port 2222\n";
    std::ofstream(home / ".ssh" / "known_hosts") << "[box.example.com]:2222 ssh-ed25519 AAAA\n"
                                                 << "[other.example.com]:2222 ssh-ed25519 AAAA\n"
                                                 << "box.example.com ssh-ed25519 AAAA\n";
    const HostSources sources{.home = home, .userConfig = home / ".ssh" / "config", .systemConfig = {}, .knownHosts = {home / ".ssh" / "known_hosts"}};
    CHECK(KnownHosts(sources) == Names{"box", "ssh://other.example.com:2222", "box.example.com"});
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
