#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "Editor/Host/Hello.h"
#include "Editor/Host/SearchCodec.h"
#include "Editor/PostQueue.h"
#include "Editor/Protocol/FrameTransport.h"
#include "Editor/Remote/Connection.h"
#include "EnvOverride.h"

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using ned::editor::PostQueue;
using ned::editor::protocol::FrameTransport;
using ned::editor::protocol::RemoteError;
using ned::editor::protocol::ResponseHandlers;
using ned::editor::remote::ConnectError;
using ned::editor::remote::Connection;
using ned::editor::remote::ConnectOptions;
using ned::editor::remote::Connector;
using ned::editor::remote::Handshake;
using ned::editor::remote::SayHello;
using ned::editor::remote::SshMaster;
using ned::editor::remote::SshOptions;
namespace fs         = std::filesystem;
namespace host       = ned::editor::host;
namespace error_code = ned::editor::protocol::error_code;
using Json           = nlohmann::json;

namespace {

std::string AgentBinary() {
    const char* override = std::getenv("NED_TEST_AGENT");
    return override != nullptr && *override != '\0' ? override : NED_AGENT_BINARY;
}

fs::path FreshDir() {
    std::string pattern = (fs::temp_directory_path() / "ned-connect-XXXXXX").string();
    REQUIRE(::mkdtemp(pattern.data()) != nullptr);
    return pattern;
}

template <typename Done>
bool PumpUntil(PostQueue& queue, Done done, std::chrono::milliseconds limit = std::chrono::seconds(30)) {
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

void WriteScript(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "#!/bin/sh\n"
                        << body << '\n';
    fs::permissions(path, fs::perms::owner_all);
}

// A host reached through the fake ssh, with its own data and runtime
// directories and the local agents under agents/.
struct FakeHost {
    fs::path                      dir = FreshDir();
    ned::tests::ScopedEnvOverride data{"XDG_DATA_HOME", (dir / "data").c_str()};
    ned::tests::ScopedEnvOverride runtime{"XDG_RUNTIME_DIR", (dir / "run").c_str()};
    fs::path                      local = dir / "agents" / host::BuildArch() / "ned-agent";

    FakeHost() {
        fs::create_directories(dir / "run");
        fs::create_directories(local.parent_path());
        fs::copy_file(AgentBinary(), local);
    }
    ~FakeHost() {
        fs::remove_all(dir);
    }

    [[nodiscard]] ConnectOptions Options(std::string destination = "host") const {
        return ConnectOptions{.destination = std::move(destination),
                              .ssh         = SshOptions{.program = NED_FAKE_SSH},
                              .agents      = {.environment = dir / "agents", .executableRelative = std::nullopt, .installed = "/nonexistent"}};
    }
};

host::HelloRequest Request() {
    return host::HelloRequest{.codecs = {"zstd", "none"}, .window = 16};
}

} // namespace

TEST_CASE("Connect deploys the agent, says hello, and hands over a live connection", "[Remote][Connection][HostAgent]") {
    FakeHost  fake;
    Handshake handshake = ned::editor::remote::Connect(fake.Options());
    REQUIRE(handshake.agent.uploaded);
    REQUIRE(handshake.hello.build == handshake.agent.build);
    REQUIRE(handshake.hello.codec == host::kCodecZstd);
    const fs::path controlPath = handshake.master->ControlPath();
    REQUIRE(fs::is_socket(controlPath));

    std::ofstream(fake.dir / "a.txt") << "needle\n";
    PostQueue queue;
    {
        Connection connection(std::move(handshake), queue);
        REQUIRE(connection.Destination() == "host");
        std::vector<Json> chunks;
        bool              answered = false;
        connection.Endpoint().Request(std::string(host::kSearchProject),
                                      host::ToJson(host::SearchRequest{.root = fake.dir, .pattern = "needle", .live = {}}),
                                      ResponseHandlers{[&](const Json&) { answered = true; }, [&](const RemoteError&) { answered = true; },
                                                       [&](Json data) { chunks.push_back(std::move(data)); }});
        REQUIRE(PumpUntil(queue, [&] { return answered; }));
        REQUIRE(chunks.size() == 1);
        REQUIRE(host::MatchesFromHost(chunks.front(), fake.dir).size() == 1);
        REQUIRE(connection.Connected());
    }
    REQUIRE_FALSE(fs::exists(controlPath));

    // Already installed: nothing goes up the second time.
    REQUIRE_FALSE(ned::editor::remote::Connect(fake.Options()).agent.uploaded);
}

TEST_CASE("A hello from another build reinstalls once, and a second mismatch fails", "[Remote][Connection][HostAgent]") {
    FakeHost       fake;
    const fs::path installed = ned::editor::remote::Connect(fake.Options()).agent.remotePath;
    // Damaged once nothing runs it: a running executable can't be opened for writing.
    std::ofstream(installed, std::ios::binary | std::ios::app) << "damage";
    REQUIRE(host::FileBuildId(installed) != host::FileBuildId(fake.local));

    const Handshake repaired = ned::editor::remote::Connect(fake.Options());
    REQUIRE(repaired.agent.uploaded);
    REQUIRE(repaired.hello.build == repaired.agent.build);

    // A wrapper answers with the real agent's build, never its own.
    WriteScript(fake.local, "exec '" + AgentBinary() + "' \"$@\"");
    REQUIRE_THROWS_WITH(ned::editor::remote::Connect(fake.Options()), StartsWith("the agent on host still reports build " + host::FileBuildId(AgentBinary())));
}

TEST_CASE("An agent that dies before hello says why", "[Remote][Connection]") {
    FakeHost fake;
    WriteScript(fake.local, "echo 'agent exploded' >&2; exit 3");
    REQUIRE_THROWS_WITH(ned::editor::remote::Connect(fake.Options()), "host: agent exploded");
}

TEST_CASE("Waiting for hello gives up on timeout and on cancel", "[Remote][Connection]") {
    FrameTransport silent({"sleep", "10"}, true);
    REQUIRE_THROWS_WITH(SayHello(silent, Request(), std::chrono::milliseconds(100)), "the agent didn't answer hello within 100 ms");

    FrameTransport   another({"sleep", "10"}, true);
    std::stop_source stop;
    stop.request_stop();
    REQUIRE_THROWS_WITH(SayHello(another, Request(), std::chrono::seconds(30), stop.get_token()), "cancelled waiting for the agent's hello");
}

TEST_CASE("A Connector connects off the main thread and can be cancelled", "[Remote][Connection][HostAgent]") {
    FakeHost  fake;
    PostQueue queue;

    std::optional<Connector::Result> result;
    {
        const Connector connector(fake.Options(), queue, [&](Connector::Result r) { result = std::move(r); });
        REQUIRE(PumpUntil(queue, [&] { return result.has_value(); }));
    }
    REQUIRE(result->has_value());
    REQUIRE((**result)->Hello().build == (**result)->Agent().build);
    result.reset();

    // "silent" never logs in.
    {
        Connector connector(fake.Options("silent"), queue, [&](Connector::Result r) { result = std::move(r); });
        connector.Cancel();
        REQUIRE(PumpUntil(queue, [&] { return result.has_value(); }));
    }
    REQUIRE_FALSE(result->has_value());
    REQUIRE(result->error() == "ssh to silent cancelled");
    result.reset();

    // Destroyed mid-attempt: cancelled, and done never runs.
    {
        const Connector connector(fake.Options("silent"), queue, [&](Connector::Result r) { result = std::move(r); });
    }
    REQUIRE_FALSE(PumpUntil(queue, [&] { return result.has_value(); }, std::chrono::milliseconds(200)));
}

TEST_CASE("A connection that breaks fails its requests and reports why", "[Remote][Connection]") {
    const fs::path dir = FreshDir();
    PostQueue      queue;

    const auto connect = [&](int& peerRead, int& peerWrite) {
        int toEditor[2];
        int toPeer[2];
        REQUIRE(::pipe(toEditor) == 0);
        REQUIRE(::pipe(toPeer) == 0);
        peerRead  = toPeer[0];
        peerWrite = toEditor[1];
        return std::make_unique<Connection>(Handshake{.master    = std::make_unique<SshMaster>("host", SshOptions{.program = NED_FAKE_SSH}, dir / "m"),
                                                      .transport = FrameTransport(toEditor[0], toPeer[1]),
                                                      .hello     = {},
                                                      .agent     = {}},
                                            queue);
    };

    SECTION("the agent hangs up") {
        int        peerRead   = -1;
        int        peerWrite  = -1;
        const auto connection = connect(peerRead, peerWrite);

        std::optional<std::string> reason;
        std::optional<RemoteError> failed;
        connection->SetOnDisconnected([&](const std::string& why) { reason = why; });
        connection->Endpoint().Request("search/project", Json::object(),
                                       ResponseHandlers{[](const Json&) {}, [&](RemoteError error) { failed = std::move(error); }, {}});
        ::close(peerWrite);
        REQUIRE(PumpUntil(queue, [&] { return reason.has_value(); }));
        REQUIRE_FALSE(connection->Connected());
        REQUIRE(failed.has_value());
        REQUIRE(failed->code == error_code::kConnectionClosed);
        ::close(peerRead);
    }

    SECTION("the agent breaks the protocol") {
        int        peerRead   = -1;
        int        peerWrite  = -1;
        const auto connection = connect(peerRead, peerWrite);

        std::optional<std::string> reason;
        connection->SetOnDisconnected([&](const std::string& why) { reason = why; });
        const std::string garbage = ned::editor::protocol::EncodeFrame("not cbor", false);
        REQUIRE(::write(peerWrite, garbage.data(), garbage.size()) == static_cast<ssize_t>(garbage.size()));
        REQUIRE(PumpUntil(queue, [&] { return reason.has_value(); }));
        REQUIRE_FALSE(connection->Connected());
        ::close(peerWrite);
        ::close(peerRead);
    }
    fs::remove_all(dir);
}
