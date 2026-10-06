//
// A remote host for tests: ssh is ned_fake_ssh, so the "host" is this
// machine with its own data and runtime directories, and the agent it
// deploys is the one this build made (or $NED_TEST_AGENT).
//

#ifndef NED_TESTS_FAKEREMOTEHOST_H
#define NED_TESTS_FAKEREMOTEHOST_H

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <poll.h>
#include <unistd.h>

#include "Editor/Host/Hello.h"
#include "Editor/PostQueue.h"
#include "Editor/Remote/Connection.h"
#include "Editor/Remote/HostLookup.h"
#include "EnvOverride.h"

namespace ned::tests {

inline std::string AgentBinary() {
    const char* override = std::getenv("NED_TEST_AGENT");
    return override != nullptr && *override != '\0' ? override : NED_AGENT_BINARY;
}

inline std::filesystem::path FreshRemoteDir() {
    std::string pattern = (std::filesystem::temp_directory_path() / "ned-connect-XXXXXX").string();
    REQUIRE(::mkdtemp(pattern.data()) != nullptr);
    return pattern;
}

// Runs posted work until done() holds; false if limit passes first.
template <typename Done>
bool PumpUntil(editor::PostQueue& queue, Done done, std::chrono::milliseconds limit = std::chrono::seconds(30)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        pollfd wake{.fd = queue.WakeFd(), .events = POLLIN, .revents = 0};
        ::poll(&wake, 1, 50);
        queue.DiscardWakeups();
        queue.DrainPosted_();
    }
    return true;
}

// A resolver that knows names starting with "good." and never answers for
// ones starting with "slow.", searching domains under dir/resolv.conf.
inline editor::remote::HostLookupOptions FakeResolver(const std::filesystem::path& dir) {
    return editor::remote::HostLookupOptions{
        .resolvConf = dir / "resolv.conf",
        .command    = {"/bin/sh", "-c", "case \"$1\" in good.*) echo \"192.0.2.1 STREAM $1\" ;; slow.*) exec sleep 30 ;; *) exit 2 ;; esac", "sh"},
        .delay      = std::chrono::milliseconds(20),
        .limit      = std::chrono::seconds(3)};
}

struct FakeRemoteHost {
    std::filesystem::path dir = FreshRemoteDir();
    ScopedEnvOverride     data{"XDG_DATA_HOME", (dir / "data").c_str()};
    ScopedEnvOverride     runtime{"XDG_RUNTIME_DIR", (dir / "run").c_str()};
    std::filesystem::path local = dir / "agents" / editor::host::BuildArch() / "ned-agent";

    FakeRemoteHost() {
        std::filesystem::create_directories(dir / "run");
        std::filesystem::create_directories(local.parent_path());
        std::filesystem::copy_file(AgentBinary(), local);
    }
    ~FakeRemoteHost() {
        std::filesystem::remove_all(dir);
    }
    FakeRemoteHost(const FakeRemoteHost&)            = delete;
    FakeRemoteHost& operator=(const FakeRemoteHost&) = delete;

    [[nodiscard]] editor::remote::ConnectOptions Options(std::string destination = "host") const {
        return editor::remote::ConnectOptions{
            .destination = std::move(destination),
            .ssh         = editor::remote::SshOptions{.program = NED_FAKE_SSH},
            .agents      = {.environment = dir / "agents", .executableRelative = std::nullopt, .installed = "/nonexistent"}};
    }
};

} // namespace ned::tests

#endif // NED_TESTS_FAKEREMOTEHOST_H
