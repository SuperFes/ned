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
#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/FrameTransport.h"
#include "Editor/Remote/Connection.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/Location.h"
#include "Editor/Remote/Search.h"
#include "EnvOverride.h"
#include "FakeRemoteHost.h"

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
using ned::editor::remote::Hosts;
using ned::editor::remote::SayHello;
using ned::editor::remote::SshMaster;
using ned::editor::remote::SshOptions;
namespace fs         = std::filesystem;
namespace host       = ned::editor::host;
namespace error_code = ned::editor::protocol::error_code;
using Json           = nlohmann::json;

namespace {

void WriteScript(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "#!/bin/sh\n"
                        << body << '\n';
    fs::permissions(path, fs::perms::owner_all);
}

using FakeHost = ned::tests::FakeRemoteHost;
using ned::tests::AgentBinary;
using ned::tests::PumpUntil;

fs::path FreshDir() {
    return ned::tests::FreshRemoteDir();
}

host::HelloRequest Request() {
    return host::HelloRequest{.codecs = {"zstd", "none"}, .window = 16};
}

// A Connection over pipes, with the test playing the agent.
struct PipePeer {
    int                         fromEditor = -1;
    int                         toEditor   = -1;
    std::unique_ptr<Connection> connection;

    PipePeer(PostQueue& queue, const fs::path& dir) {
        int editorIn[2];
        int editorOut[2];
        REQUIRE(::pipe(editorIn) == 0);
        REQUIRE(::pipe(editorOut) == 0);
        fromEditor = editorOut[0];
        toEditor   = editorIn[1];
        connection = std::make_unique<Connection>(Handshake{.master    = std::make_unique<SshMaster>("host", SshOptions{.program = NED_FAKE_SSH}, dir / "m"),
                                                            .transport = FrameTransport(editorIn[0], editorOut[1]),
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
    void Send(const Json& message) const {
        SendRaw(ned::editor::protocol::EncodeFrame(ned::editor::protocol::EncodeCbor(message), false));
    }
    void HangUp() {
        if (toEditor >= 0) {
            ::close(toEditor);
            toEditor = -1;
        }
    }
};

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

TEST_CASE("A host that already has the agent starts it in one session", "[Remote][Connection][HostAgent]") {
    FakeHost fake;
    // Offered too, but never this host's architecture.
    const fs::path foreign = fake.dir / "agents" / "sparc64" / "ned-agent";
    fs::create_directories(foreign.parent_path());
    fs::copy_file(fake.local, foreign);
    const fs::path                      log = fake.dir / "sessions";
    const ned::tests::ScopedEnvOverride logged("NED_FAKE_SSH_LOG", log.c_str());
    const auto                          sessions = [&log] {
        std::ifstream in(log);
        std::size_t   count = 0;
        for (std::string line; std::getline(in, line);) {
            ++count;
        }
        return count;
    };

    // Missing, so a probe and an upload come between two starts.
    REQUIRE(ned::editor::remote::Connect(fake.Options()).agent.uploaded);
    REQUIRE(sessions() == 4);

    const Handshake again = ned::editor::remote::Connect(fake.Options());
    REQUIRE(sessions() == 5);
    REQUIRE_FALSE(again.agent.uploaded);
    REQUIRE(again.agent.arch == host::BuildArch());
    REQUIRE(again.agent.build == host::FileBuildId(fake.local));
    REQUIRE(again.agent.remotePath == fake.dir / "data" / "ned" / "agent" / (again.agent.build + "-" + again.agent.arch) / "ned-agent");
    REQUIRE(again.hello.build == again.agent.build);
}

TEST_CASE("Whatever a login script prints ahead of the agent is skipped", "[Remote][Connection][HostAgent]") {
    FakeHost        fake;
    const Handshake handshake = ned::editor::remote::Connect(fake.Options("chatty"));
    REQUIRE(handshake.hello.build == handshake.agent.build);
}

TEST_CASE("Waiting for the agent to start reads where it is, and gives up on a flood, an exit, a timeout and a cancel", "[Remote][Connection]") {
    using ned::editor::remote::AwaitAgentStart;
    using ned::editor::remote::kAgentStartMarker;

    using ned::editor::remote::kAgentMissingMarker;
    FrameTransport started({"/bin/sh", "-c", "printf 'motd'; printf '" + std::string(kAgentStartMarker) + " /data/ab-x86_64/ned-agent'; echo; sleep 10"}, true);
    REQUIRE(AwaitAgentStart(started, std::chrono::seconds(10)) == fs::path("/data/ab-x86_64/ned-agent"));

    FrameTransport missing({"/bin/sh", "-c", "echo motd; echo " + std::string(kAgentMissingMarker)}, true);
    REQUIRE(AwaitAgentStart(missing, std::chrono::seconds(10)) == std::nullopt);

    FrameTransport flood({"/bin/sh", "-c", "yes 'You have new mail.'"}, true);
    REQUIRE_THROWS_WITH(AwaitAgentStart(flood, std::chrono::seconds(10)), ContainsSubstring("does a login script write to stdout? It began: You have new mail."));

    FrameTransport exited({"/bin/sh", "-c", "echo 'sh: exec: ned-agent: not found' >&2; exit 127"}, true);
    REQUIRE_THROWS_WITH(AwaitAgentStart(exited, std::chrono::seconds(10)), "sh: exec: ned-agent: not found");

    FrameTransport silent({"sleep", "10"}, true);
    REQUIRE_THROWS_WITH(AwaitAgentStart(silent, std::chrono::milliseconds(100)), "the agent didn't start within 100 ms");

    FrameTransport   another({"sleep", "10"}, true);
    std::stop_source stop;
    stop.request_stop();
    REQUIRE_THROWS_WITH(AwaitAgentStart(another, std::chrono::seconds(30), stop.get_token()), "cancelled waiting for the agent to start");
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

    SECTION("the agent hangs up") {
        PipePeer peer(queue, dir);

        std::optional<std::string> reason;
        std::optional<RemoteError> failed;
        peer.connection->SetOnDisconnected([&](const std::string& why) { reason = why; });
        peer.connection->Endpoint().Request("search/project", Json::object(),
                                            ResponseHandlers{[](const Json&) {}, [&](RemoteError error) { failed = std::move(error); }, {}});
        peer.HangUp();
        REQUIRE(PumpUntil(queue, [&] { return reason.has_value(); }));
        REQUIRE_FALSE(peer.connection->Connected());
        REQUIRE(failed.has_value());
        REQUIRE(failed->code == error_code::kConnectionClosed);
    }

    SECTION("the agent breaks the protocol") {
        PipePeer peer(queue, dir);

        std::optional<std::string> reason;
        peer.connection->SetOnDisconnected([&](const std::string& why) { reason = why; });
        peer.SendRaw(ned::editor::protocol::EncodeFrame("not cbor", false));
        REQUIRE(PumpUntil(queue, [&] { return reason.has_value(); }));
        REQUIRE_FALSE(peer.connection->Connected());
    }
    fs::remove_all(dir);
}

TEST_CASE("Hosts share an attempt per destination and keep its connection", "[Remote][Connection][HostAgent]") {
    FakeHost  fake;
    PostQueue queue;
    Hosts     hosts(queue, [&](const std::string& destination) { return fake.Options(destination); });

    std::vector<Connection*> answers;
    const auto               collect = [&](std::expected<Connection*, std::string> connection) {
        REQUIRE(connection.has_value());
        answers.push_back(*connection);
    };
    hosts.Connect("host", collect);
    hosts.Connect("host", collect);
    REQUIRE(hosts.Connecting("host"));
    REQUIRE(PumpUntil(queue, [&] { return answers.size() == 2; }));
    REQUIRE(answers[0] == answers[1]);
    REQUIRE(hosts.Find("host") == answers[0]);
    REQUIRE(hosts.Destinations() == std::vector<std::string>{"host"});
    REQUIRE_FALSE(hosts.Connecting("host"));

    // Already connected: answered at once.
    hosts.Connect("host", collect);
    REQUIRE(answers.size() == 3);
    REQUIRE(answers[2] == answers[0]);

    std::optional<RemoteError> failed;
    hosts.Find("host")->Endpoint().Request("search/project", host::ToJson(host::SearchRequest{.root = "/nonexistent", .pattern = "x", .live = {}}),
                                           ResponseHandlers{[](const Json&) {}, [&](RemoteError error) { failed = std::move(error); }, {}});
    bool dropped = false;
    hosts.SetOnDropped([&](const std::string&, const std::string&) { dropped = true; });
    REQUIRE(hosts.Disconnect("host"));
    REQUIRE(failed.has_value());
    REQUIRE(failed->code == error_code::kConnectionClosed);
    REQUIRE(failed->message == "disconnected from host");
    REQUIRE(hosts.Find("host") == nullptr);
    REQUIRE(hosts.Destinations().empty());
    REQUIRE_FALSE(hosts.Disconnect("host"));
    REQUIRE_FALSE(dropped);
}

TEST_CASE("Hosts report attempts that fail or are cancelled, and connections that drop", "[Remote][Connection][HostAgent]") {
    FakeHost  fake;
    PostQueue queue;
    Hosts     hosts(queue, [&](const std::string& destination) { return fake.Options(destination); });

    std::optional<std::expected<Connection*, std::string>> denied;
    hosts.Connect("denied", [&](std::expected<Connection*, std::string> connection) { denied = connection; });
    REQUIRE(PumpUntil(queue, [&] { return denied.has_value(); }));
    REQUIRE_FALSE(denied->has_value());
    REQUIRE(hosts.Destinations().empty());

    std::optional<std::expected<Connection*, std::string>> silent;
    hosts.Connect("silent", [&](std::expected<Connection*, std::string> connection) { silent = connection; });
    REQUIRE(hosts.Disconnect("silent"));
    REQUIRE(silent.has_value());
    REQUIRE(silent->error() == "cancelled");
    REQUIRE_FALSE(hosts.Connecting("silent"));

    std::optional<std::pair<std::string, std::string>> dropped;
    hosts.SetOnDropped([&](const std::string& destination, const std::string& reason) { dropped.emplace(destination, reason); });
    Connection* live = nullptr;
    hosts.Connect("host", [&](std::expected<Connection*, std::string> connection) { live = connection.value(); });
    REQUIRE(PumpUntil(queue, [&] { return live != nullptr; }));
    live->Close("the link went away");
    REQUIRE(dropped == std::pair<std::string, std::string>{"host", "the link went away"});
    REQUIRE(hosts.Find("host") == nullptr);
    REQUIRE(PumpUntil(queue, [&] { return hosts.Destinations().empty(); }));
}

TEST_CASE("A cancelled attempt never yields its connection, and the next one starts afresh", "[Remote][Connection][HostAgent]") {
    FakeHost  fake;
    PostQueue queue;
    Hosts     hosts(queue, [&](const std::string& destination) { return fake.Options(destination); });

    std::vector<std::expected<Connection*, std::string>> answers;
    const auto                                           collect = [&](std::expected<Connection*, std::string> connection) { answers.push_back(std::move(connection)); };
    hosts.Connect("host", collect);
    // Logged in and posted, but not yet handed over.
    pollfd posted{.fd = queue.WakeFd(), .events = POLLIN, .revents = 0};
    REQUIRE(::poll(&posted, 1, 30000) == 1);
    REQUIRE(hosts.Disconnect("host"));
    REQUIRE(answers.size() == 1);
    REQUIRE(answers[0].error() == "cancelled");

    hosts.Connect("host", collect);
    REQUIRE(hosts.Connecting("host"));
    REQUIRE(PumpUntil(queue, [&] { return answers.size() == 2; }));
    REQUIRE(answers[1].has_value());
    REQUIRE(hosts.Find("host") == *answers[1]);
    REQUIRE(hosts.Destinations() == std::vector<std::string>{"host"});
}

TEST_CASE("A remote project search streams the host's matches", "[Remote][Connection][HostAgent]") {
    FakeHost fake;
    fs::create_directories(fake.dir / "tree" / "sub");
    std::ofstream(fake.dir / "tree" / "a.txt") << "needle one\nhay\nneedle two\n";
    std::ofstream(fake.dir / "tree" / "sub" / "b.txt") << "a needle\n";

    PostQueue  queue;
    Connection connection(ned::editor::remote::Connect(fake.Options()), queue);

    std::vector<ned::editor::SearchMatch>     matches;
    std::optional<std::optional<std::string>> done;
    ned::editor::remote::SearchProject(
        connection, host::SearchRequest{.root = fake.dir / "tree", .pattern = "needle", .live = {}},
        [&](std::vector<ned::editor::SearchMatch> chunk) { matches.insert(matches.end(), chunk.begin(), chunk.end()); },
        [&](std::optional<std::string> failure) { done = std::move(failure); });
    REQUIRE(PumpUntil(queue, [&] { return done.has_value(); }));
    REQUIRE(*done == std::nullopt);
    REQUIRE(matches.size() == 3);

    done.reset();
    ned::editor::remote::SearchProject(
        connection, host::SearchRequest{.root = fake.dir / "tree", .pattern = "(", .live = {}}, [](std::vector<ned::editor::SearchMatch>) {},
        [&](std::optional<std::string> failure) { done = std::move(failure); });
    REQUIRE(PumpUntil(queue, [&] { return done.has_value(); }));
    REQUIRE(done->has_value());
    REQUIRE_THAT(**done, ContainsSubstring("missing )"));
}

TEST_CASE("A remote project search refuses matches outside its root", "[Remote][Connection]") {
    const fs::path dir = FreshDir();
    PostQueue      queue;
    PipePeer       peer(queue, dir);

    std::size_t                               delivered = 0;
    std::optional<std::optional<std::string>> done;
    const std::uint64_t                       id = ned::editor::remote::SearchProject(
        *peer.connection, host::SearchRequest{.root = "/project", .pattern = "x", .live = {}},
        [&](std::vector<ned::editor::SearchMatch>) { ++delivered; }, [&](std::optional<std::string> failure) { done = std::move(failure); });

    const std::vector<ned::editor::SearchMatch> outside{{.file = "/etc/passwd", .lineNumber = 1, .lineText = "root"}};
    peer.Send(Json{{"method", "$/chunk"}, {"params", {{"id", id}, {"seq", 0}, {"data", host::MatchesToJson(outside)}}}});
    peer.Send(Json{{"id", id}, {"error", {{"code", ned::editor::protocol::error_code::kCancelled}, {"message", "cancelled"}}}});
    REQUIRE(PumpUntil(queue, [&] { return done.has_value(); }));
    REQUIRE(delivered == 0);
    REQUIRE(done->has_value());
    REQUIRE_THAT(**done, StartsWith("the host's search results are malformed: "));
    fs::remove_all(dir);
}

TEST_CASE("A remote location is a destination, a colon, then an absolute path", "[Remote]") {
    using ned::editor::remote::ParseLocation;
    const auto located = ParseLocation("me@box:/srv/app/../www/");
    REQUIRE(located.has_value());
    REQUIRE(located->destination == "me@box");
    REQUIRE(located->path == "/srv/www");
    REQUIRE(located->ToString() == "me@box:/srv/www");
    REQUIRE(ParseLocation("box:/")->path == "/");
    REQUIRE(ParseLocation("[::1]:/tmp")->destination == "[::1]");

    REQUIRE_FALSE(ParseLocation("box"));
    REQUIRE_FALSE(ParseLocation("box:relative"));
    REQUIRE_FALSE(ParseLocation(":/tmp"));
}
