#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "Editor/Host/Hello.h"
#include "Editor/Remote/Deploy.h"
#include "EnvOverride.h"

using ned::editor::remote::AgentDirCandidates;
using ned::editor::remote::DefaultAgentDirCandidates;
using ned::editor::remote::DeployedAgent;
using ned::editor::remote::DeployError;
using ned::editor::remote::EnsureAgent;
using ned::editor::remote::FindLocalAgent;
using ned::editor::remote::ParseProbe;
using ned::editor::remote::RemoteHost;
using ned::editor::remote::SshMaster;
using ned::editor::remote::SshOptions;
namespace fs   = std::filesystem;
namespace host = ned::editor::host;

namespace {

fs::path FreshDir() {
    std::string pattern = (fs::temp_directory_path() / "ned-deploy-XXXXXX").string();
    REQUIRE(::mkdtemp(pattern.data()) != nullptr);
    return pattern;
}

void WriteAgent(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << "#!/bin/sh\n"
                                          << body << '\n';
    fs::permissions(path, fs::perms::owner_all);
}

std::string Contents(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

AgentDirCandidates Only(const fs::path& dir) {
    return AgentDirCandidates{.environment = dir, .executableRelative = std::nullopt, .installed = "/nonexistent"};
}

} // namespace

TEST_CASE("The probe's answer is read past anything a login script printed", "[Remote][Deploy]") {
    const RemoteHost probed = ParseProbe("Welcome to the box!\nned-agent-probe-1\nx86_64\n/home/u/.local/share/ned/agent\nab12-x86_64\ncd34-x86_64\n");
    REQUIRE(probed.arch == "x86_64");
    REQUIRE(probed.agentRoot == "/home/u/.local/share/ned/agent");
    REQUIRE(probed.installed == std::vector<std::string>{"ab12-x86_64", "cd34-x86_64"});

    REQUIRE(ParseProbe("ned-agent-probe-1\naarch64\n/data/ned/agent\n").installed.empty());
    REQUIRE_THROWS_AS(ParseProbe("bash: sh: command not found\n"), DeployError);
    REQUIRE_THROWS_AS(ParseProbe("ned-agent-probe-1\nx86_64\nrelative/agent\n"), DeployError);
    REQUIRE_THROWS_AS(ParseProbe("ned-agent-probe-1\n"), DeployError);
}

TEST_CASE("Local agents are found per architecture, $NED_AGENT_DIR alone when set", "[Remote][Deploy]") {
    const fs::path dir = FreshDir();
    WriteAgent(dir / "env" / "aarch64" / "ned-agent", "env");
    WriteAgent(dir / "exe" / "x86_64" / "ned-agent", "exe");
    WriteAgent(dir / "installed" / "x86_64" / "ned-agent", "installed");
    WriteAgent(dir / "installed" / "riscv64" / "ned-agent", "installed");
    fs::permissions(dir / "installed" / "riscv64" / "ned-agent", fs::perms::owner_read | fs::perms::owner_write);

    const AgentDirCandidates fallback{.environment = std::nullopt, .executableRelative = dir / "exe", .installed = dir / "installed"};
    REQUIRE(FindLocalAgent(fallback, "x86_64") == dir / "exe" / "x86_64" / "ned-agent");
    REQUIRE(FindLocalAgent(fallback, "aarch64") == std::nullopt);
    // Not executable, so not an agent.
    REQUIRE(FindLocalAgent(fallback, "riscv64") == std::nullopt);

    AgentDirCandidates environment = fallback;
    environment.environment        = dir / "env";
    REQUIRE(FindLocalAgent(environment, "aarch64") == dir / "env" / "aarch64" / "ned-agent");
    REQUIRE(FindLocalAgent(environment, "x86_64") == std::nullopt);
    fs::remove_all(dir);

    // build/Tests/ned_tests looks in build/libexec/ned/agent, where the
    // agent-* builds install.
    const ned::tests::ScopedEnvOverride unset("NED_AGENT_DIR", nullptr);
    const AgentDirCandidates            defaults = DefaultAgentDirCandidates();
    REQUIRE_FALSE(defaults.environment.has_value());
    REQUIRE(defaults.executableRelative == fs::canonical("/proc/self/exe").parent_path().parent_path() / "libexec" / "ned" / "agent");
    REQUIRE(defaults.installed.filename() == "agent");
}

TEST_CASE("An agent is uploaded once per build, owner-only, and again on request", "[Remote][Deploy]") {
    const fs::path                      dir = FreshDir();
    const ned::tests::ScopedEnvOverride data("XDG_DATA_HOME", (dir / "data").c_str());
    const fs::path                      local = dir / "agents" / host::BuildArch() / "ned-agent";
    WriteAgent(local, "echo one");

    SshMaster master("host", SshOptions{.program = NED_FAKE_SSH}, dir / "m");
    master.Start();

    const DeployedAgent first = EnsureAgent(master, Only(dir / "agents"));
    REQUIRE(first.uploaded);
    REQUIRE(first.arch == host::BuildArch());
    REQUIRE(first.build == host::FileBuildId(local));
    const fs::path installed = dir / "data" / "ned" / "agent" / (first.build + "-" + first.arch);
    REQUIRE(first.remotePath == installed / "ned-agent");
    REQUIRE(Contents(first.remotePath) == Contents(local));
    REQUIRE(fs::status(first.remotePath).permissions() == fs::perms::owner_all);
    REQUIRE(fs::status(installed).permissions() == fs::perms::owner_all);
    REQUIRE(fs::status(dir / "data" / "ned").permissions() == fs::perms::owner_all);
    // Only the agent: the upload's temporary is gone.
    REQUIRE(std::distance(fs::directory_iterator(installed), fs::directory_iterator()) == 1);

    REQUIRE_FALSE(EnsureAgent(master, Only(dir / "agents")).uploaded);
    REQUIRE(EnsureAgent(master, Only(dir / "agents"), true).uploaded);

    // A new local build installs beside the old one.
    WriteAgent(local, "echo two");
    const DeployedAgent second = EnsureAgent(master, Only(dir / "agents"));
    REQUIRE(second.uploaded);
    REQUIRE(second.remotePath != first.remotePath);
    REQUIRE(Contents(second.remotePath) == Contents(local));
    fs::remove_all(dir);
}

TEST_CASE("Deploy failures say what went wrong", "[Remote][Deploy]") {
    const fs::path                      dir = FreshDir();
    const ned::tests::ScopedEnvOverride data("XDG_DATA_HOME", (dir / "data").c_str());
    SshMaster                           master("host", SshOptions{.program = NED_FAKE_SSH}, dir / "m");
    master.Start();

    fs::create_directories(dir / "agents");
    REQUIRE_THROWS_WITH(EnsureAgent(master, Only(dir / "agents")), "no agent for " + host::BuildArch() + " to run on host");

    WriteAgent(dir / "agents" / host::BuildArch() / "ned-agent", "echo agent");
    fs::create_directories(dir / "data");
    fs::permissions(dir / "data", fs::perms::owner_read | fs::perms::owner_exec);
    REQUIRE_THROWS_WITH(EnsureAgent(master, Only(dir / "agents")), Catch::Matchers::StartsWith("installing the agent on host failed: mkdir"));
    fs::permissions(dir / "data", fs::perms::owner_all);
    fs::remove_all(dir);
}
