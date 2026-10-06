#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Remote/Ssh.h"
#include "Editor/RuntimeDir.h"
#include "EnvOverride.h"

using ned::editor::process::ChildProcess;
using ned::editor::remote::NewControlPath;
using ned::editor::remote::QuoteRemoteWord;
using ned::editor::remote::RemoteCommandLine;
using ned::editor::remote::RunSession;
using ned::editor::remote::SessionResult;
using ned::editor::remote::SshError;
using ned::editor::remote::SshMaster;
using ned::editor::remote::SshOptions;
using ned::editor::remote::SshSessionArgv;
namespace remote = ned::editor::remote;

namespace {

SshOptions FakeSsh() {
    return SshOptions{.program = NED_FAKE_SSH};
}

// A fresh, short directory: control sockets must fit sun_path.
std::filesystem::path FreshDir() {
    std::string pattern = (std::filesystem::temp_directory_path() / "ned-ssh-XXXXXX").string();
    REQUIRE(::mkdtemp(pattern.data()) != nullptr);
    return pattern;
}

struct Output {
    std::string        text;
    std::optional<int> exitCode;
};

Output Run(const std::vector<std::string>& argv) {
    ChildProcess child(argv);
    Output       output;
    for (std::string chunk = child.ReadSome(); !chunk.empty(); chunk = child.ReadSome()) {
        output.text += chunk;
    }
    output.exitCode = child.WaitForExit();
    return output;
}

bool HasOption(const std::vector<std::string>& argv, const std::string& flag, const std::string& value) {
    for (std::size_t i = 0; i + 1 < argv.size(); ++i) {
        if (argv[i] == flag && argv[i + 1] == value) {
            return true;
        }
    }
    return false;
}

std::string StartError(SshMaster& master, const std::stop_token& stop = {}) {
    try {
        master.Start(stop);
    }
    catch (const SshError& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST_CASE("Remote command words are quoted only where a shell would read them differently", "[Remote][Ssh]") {
    REQUIRE(QuoteRemoteWord("/home/user/.local/share/ned/agent/ab12-x86_64/ned-agent") == "/home/user/.local/share/ned/agent/ab12-x86_64/ned-agent");
    REQUIRE(QuoteRemoteWord("--stdio") == "--stdio");
    REQUIRE(QuoteRemoteWord("") == "''");
    REQUIRE(QuoteRemoteWord("a b") == "'a b'");
    REQUIRE(QuoteRemoteWord("$HOME") == "'$HOME'");
    REQUIRE(QuoteRemoteWord("it's") == R"('it'\''s')");
    REQUIRE(RemoteCommandLine(std::vector<std::string>{"ned-agent", "--stdio", "my dir"}) == "ned-agent --stdio 'my dir'");
}

TEST_CASE("ssh argv overrides what breaks a non-interactive pipe", "[Remote][Ssh]") {
    const SshOptions               options;
    const std::vector<std::string> session = SshSessionArgv("-oProxyCommand=evil", "/run/user/1000/ned/ssh/1-0", options, std::vector<std::string>{"ned-agent", "--stdio"});

    REQUIRE(session.front() == "ssh");
    REQUIRE(HasOption(session, "-S", "/run/user/1000/ned/ssh/1-0"));
    REQUIRE(HasOption(session, "-o", "BatchMode=yes"));
    REQUIRE(HasOption(session, "-o", "RemoteCommand=none"));
    REQUIRE(HasOption(session, "-o", "RequestTTY=no"));
    REQUIRE(HasOption(session, "-o", "ControlMaster=no"));
    REQUIRE(std::ranges::count(session, "-T") == 1);
    // The destination follows "--", so a hostile one can't be read as an option.
    REQUIRE(session.size() >= 3);
    REQUIRE(session[session.size() - 3] == "--");
    REQUIRE(session[session.size() - 2] == "-oProxyCommand=evil");
    REQUIRE(session.back() == "ned-agent --stdio");

    const std::vector<std::string> master = remote::SshMasterArgv("host", "/run/50%/1-0", options);
    REQUIRE(HasOption(master, "-S", "/run/50%%/1-0"));
    REQUIRE(HasOption(master, "-o", "ControlPersist=no"));
    REQUIRE(std::ranges::count(master, "-M") == 1);
    REQUIRE(std::ranges::count(master, "-N") == 1);
    REQUIRE(master.back() == "host");

    const std::vector<std::string> exit = remote::SshExitArgv("host", "/run/1-0", options);
    REQUIRE(HasOption(exit, "-O", "exit"));
    REQUIRE(exit.back() == "host");
}

TEST_CASE("Sessions run through a master that logs in once", "[Remote][Ssh]") {
    const std::filesystem::path dir     = FreshDir();
    const std::filesystem::path control = dir / "m";
    {
        SshMaster master("host", FakeSsh(), control);
        master.Start();
        REQUIRE(std::filesystem::is_socket(control));

        // The remote shell sees each word exactly as given.
        const Output echoed = Run(master.SessionArgv(std::vector<std::string>{"printf", "%s\\n", "it's", "$HOME", "a  b", "*", ""}));
        REQUIRE(echoed.exitCode == 0);
        REQUIRE(echoed.text == "it's\n$HOME\na  b\n*\n\n");

        master.Stop();
        REQUIRE_FALSE(std::filesystem::exists(control));
        REQUIRE(Run(master.SessionArgv(std::vector<std::string>{"true"})).exitCode == 255);

        master.Start();
        REQUIRE(Run(master.SessionArgv(std::vector<std::string>{"true"})).exitCode == 0);
    }
    // Destruction stops the master too.
    REQUIRE_FALSE(std::filesystem::exists(control));
    std::filesystem::remove_all(dir);
}

TEST_CASE("A refused login reports ssh's own message", "[Remote][Ssh]") {
    const std::filesystem::path dir = FreshDir();
    SshMaster                   master("denied", FakeSsh(), dir / "m");

    REQUIRE(StartError(master) == "fake@denied: Permission denied (publickey).");
    REQUIRE_FALSE(std::filesystem::exists(dir / "m"));

    SshOptions missing = FakeSsh();
    missing.program    = (dir / "no-such-ssh").string();
    SshMaster absent("host", missing, dir / "m");
    REQUIRE(StartError(absent).starts_with("cannot run " + missing.program));
    std::filesystem::remove_all(dir);
}

TEST_CASE("A login that never finishes times out or is cancelled", "[Remote][Ssh]") {
    const std::filesystem::path dir = FreshDir();

    SshOptions quick   = FakeSsh();
    quick.startTimeout = std::chrono::milliseconds(200);
    SshMaster timedOut("silent", quick, dir / "m");
    REQUIRE(StartError(timedOut) == "ssh to silent didn't log in within 200 ms");

    SshMaster        cancelled("silent", FakeSsh(), dir / "m");
    std::stop_source stop;
    const auto       started = std::chrono::steady_clock::now();
    std::jthread     canceller([&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop.request_stop();
    });
    REQUIRE(StartError(cancelled, stop.get_token()) == "ssh to silent cancelled");
    REQUIRE(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Control paths are unique and sit in an owner-only runtime directory", "[Remote][Ssh]") {
    const std::filesystem::path         dir = FreshDir();
    const ned::tests::ScopedEnvOverride runtime("XDG_RUNTIME_DIR", dir.c_str());

    const std::filesystem::path first  = NewControlPath();
    const std::filesystem::path second = NewControlPath();
    REQUIRE(first != second);
    REQUIRE(first.parent_path() == dir / "ned" / "ssh");
    REQUIRE(second.parent_path() == first.parent_path());
    REQUIRE(std::filesystem::status(dir / "ned" / "ssh").permissions() == std::filesystem::perms::owner_all);
    REQUIRE(std::filesystem::status(dir / "ned").permissions() == std::filesystem::perms::owner_all);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Masters whose editor is gone are asked to exit, live editors' are kept", "[Remote][Ssh]") {
    const std::filesystem::path         dir = FreshDir();
    const ned::tests::ScopedEnvOverride runtime("XDG_RUNTIME_DIR", dir.c_str());
    REQUIRE(remote::SweepOrphanedMasters(FakeSsh()) == 0);

    pid_t deadPid = 0;
    {
        ChildProcess gone(std::vector<std::string>{"true"});
        deadPid = gone.Pid();
        (void)gone.WaitForExit();
    }
    const std::filesystem::path sockets  = ned::editor::EnsureRuntimeDirectory("ssh");
    const std::filesystem::path orphaned = sockets / (std::to_string(deadPid) + "-0");
    const std::filesystem::path live     = sockets / (std::to_string(::getpid()) + "-99");
    ChildProcess                orphan(remote::SshMasterArgv("host", orphaned, FakeSsh()));
    SshMaster                   owned("host", FakeSsh(), live);
    owned.Start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!std::filesystem::is_socket(orphaned) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(std::filesystem::is_socket(orphaned));

    REQUIRE(remote::SweepOrphanedMasters(FakeSsh()) == 1);
    REQUIRE(orphan.WaitForExit() == 0);
    REQUIRE_FALSE(std::filesystem::exists(orphaned));
    REQUIRE(std::filesystem::is_socket(live));
    REQUIRE(Run(owned.SessionArgv(std::vector<std::string>{"true"})).exitCode == 0);
    owned.Stop();
    std::filesystem::remove_all(dir);
}

TEST_CASE("A one-shot session takes input and reports output, errors and exit code", "[Remote][Ssh]") {
    const std::filesystem::path dir = FreshDir();
    SshMaster                   master("host", FakeSsh(), dir / "m");
    master.Start();

    const std::string   big(3 * 1024 * 1024 + 7, 'x');
    const SessionResult echoed = RunSession(master, std::vector<std::string>{"wc", "-c"}, big);
    REQUIRE(echoed.exitCode == 0);
    REQUIRE(echoed.output == std::to_string(big.size()) + "\n");

    const SessionResult failed = RunSession(master, std::vector<std::string>{"sh", "-c", "echo oops >&2; exit 3"});
    REQUIRE(failed.exitCode == 3);
    REQUIRE(failed.output.empty());
    REQUIRE(failed.errors == "oops\n");

    // A command that quits without reading its input still says how it ended.
    REQUIRE(RunSession(master, std::vector<std::string>{"sh", "-c", "exit 7"}, big).exitCode == 7);
    std::filesystem::remove_all(dir);
}

TEST_CASE("A one-shot session that stalls or is cancelled is killed", "[Remote][Ssh]") {
    const std::filesystem::path dir = FreshDir();
    SshMaster                   master("host", FakeSsh(), dir / "m");
    master.Start();

    const auto started = std::chrono::steady_clock::now();
    REQUIRE_THROWS_WITH(RunSession(master, std::vector<std::string>{"sleep", "30"}, {}, std::chrono::milliseconds(200)), "no answer on host within 200 ms");

    std::stop_source stop;
    std::jthread     canceller([&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop.request_stop();
    });
    REQUIRE_THROWS_WITH(RunSession(master, std::vector<std::string>{"sleep", "30"}, {}, std::chrono::seconds(30), stop.get_token()), "cancelled on host");
    REQUIRE(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
    std::filesystem::remove_all(dir);
}
