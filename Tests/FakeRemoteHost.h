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
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <poll.h>
#include <unistd.h>

#include "Editor/Host/Hello.h"
#include "Editor/PostQueue.h"
#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/FrameTransport.h"
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

// A Connection over pipes, with the test playing the agent.
struct PipePeer {
    int                                     fromEditor = -1;
    int                                     toEditor   = -1;
    std::unique_ptr<editor::remote::Connection> connection;

    PipePeer(editor::PostQueue& queue, const std::filesystem::path& dir) {
        int editorIn[2];
        int editorOut[2];
        REQUIRE(::pipe(editorIn) == 0);
        REQUIRE(::pipe(editorOut) == 0);
        fromEditor = editorOut[0];
        toEditor   = editorIn[1];
        connection = std::make_unique<editor::remote::Connection>(
            editor::remote::Handshake{.master    = std::make_unique<editor::remote::SshMaster>("host", editor::remote::SshOptions{.program = NED_FAKE_SSH}, dir / "m"),
                                      .transport = editor::protocol::FrameTransport(editorIn[0], editorOut[1]),
                                      .hello     = {},
                                      .agent     = {}},
            queue);
    }
    ~PipePeer() {
        connection.reset();
        HangUp();
        ::close(fromEditor);
    }
    PipePeer(const PipePeer&)            = delete;
    PipePeer& operator=(const PipePeer&) = delete;

    void SendRaw(const std::string& frame) const {
        REQUIRE(::write(toEditor, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()));
    }
    void Send(const nlohmann::json& message) const {
        SendRaw(editor::protocol::EncodeFrame(editor::protocol::EncodeCbor(message), false));
    }
    [[nodiscard]] bool HasMessage() const {
        pollfd readable{.fd = fromEditor, .events = POLLIN, .revents = 0};
        return ::poll(&readable, 1, 0) == 1;
    }
    // The editor's next message; uncompressed, since no hello settled a codec.
    [[nodiscard]] nlohmann::json Receive() const {
        const auto readExactly = [this](std::size_t count) {
            std::string bytes(count, '\0');
            for (std::size_t got = 0; got < count;) {
                pollfd readable{.fd = fromEditor, .events = POLLIN, .revents = 0};
                REQUIRE(::poll(&readable, 1, 10000) == 1);
                const ssize_t n = ::read(fromEditor, bytes.data() + got, count - got);
                REQUIRE(n > 0);
                got += static_cast<std::size_t>(n);
            }
            return bytes;
        };
        const std::string header = readExactly(5);
        const std::size_t length = (std::size_t{static_cast<unsigned char>(header[0])} << 24) | (std::size_t{static_cast<unsigned char>(header[1])} << 16) |
                                   (std::size_t{static_cast<unsigned char>(header[2])} << 8) | std::size_t{static_cast<unsigned char>(header[3])};
        REQUIRE(header[4] == 0);
        return editor::protocol::DecodeCbor(readExactly(length));
    }
    void HangUp() {
        if (toEditor >= 0) {
            ::close(toEditor);
            toEditor = -1;
        }
    }
};

} // namespace ned::tests

#endif // NED_TESTS_FAKEREMOTEHOST_H
