#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "Editor/Host/Agent.h"
#include "Editor/Host/Codec.h"
#include "Editor/Host/FsCodec.h"
#include "Editor/Host/Hello.h"
#include "Editor/Host/ProcCodec.h"
#include "Editor/Host/SearchCodec.h"
#include "Editor/Host/WatchCodec.h"
#include "Editor/PostQueue.h"
#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/Endpoint.h"
#include "Editor/Protocol/FrameTransport.h"
#include "Editor/Protocol/FramedConnection.h"
#include "Editor/Remote/Deploy.h"
#include "Editor/Remote/Ssh.h"
#include "EnvOverride.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::LiveContent;
using ned::editor::PostQueue;
using ned::editor::SearchMatch;
using ned::editor::SearchPattern;
using ned::editor::protocol::DecodeCbor;
using ned::editor::protocol::EncodeCbor;
using ned::editor::protocol::EncodeFrame;
using ned::editor::protocol::Endpoint;
using ned::editor::protocol::FrameTransport;
using ned::editor::protocol::RemoteError;
using ned::editor::protocol::ResponseHandlers;
namespace host       = ned::editor::host;
namespace error_code = ned::editor::protocol::error_code;
using Json           = nlohmann::json;
using Connection     = ned::editor::protocol::FramedConnection<FrameTransport>;

namespace {

// NED_TEST_AGENT points these tests at another agent build, such as a
// static cross build.
std::string AgentBinary() {
    const char* override = std::getenv("NED_TEST_AGENT");
    return override != nullptr && *override != '\0' ? override : NED_AGENT_BINARY;
}

// The editor's end of a connection to an agent.
struct AgentClient {
    PostQueue                  queue;
    Connection                 connection;
    Endpoint                   endpoint;
    std::optional<std::string> disconnected;

    explicit AgentClient(FrameTransport transport) : connection(std::move(transport), queue, Connection::Options{.entityNoun = "agent"}),
                                                     endpoint([this](const Json& message) { connection.SendFrame(EncodeCbor(message)); }) {
        connection.SetOnFrame([this](const std::string& frame) { endpoint.Receive(DecodeCbor(frame)); });
        connection.SetOnDisconnected([this](const std::string& reason) {
            disconnected = reason;
            endpoint.Close(reason);
        });
    }

    template <typename Done>
    bool PumpUntil(Done done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
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

    // Requests method and pumps until it's answered.
    struct Answer {
        std::optional<Json>        result;
        std::optional<RemoteError> error;
        std::vector<Json>          chunks;
    };
    Answer Call(const std::string& method, Json params) {
        Answer answer;
        bool   answered = false;
        endpoint.Request(method, std::move(params),
                         ResponseHandlers{[&](Json result) {
                                              answer.result = std::move(result);
                                              answered      = true;
                                          },
                                          [&](RemoteError error) {
                                              answer.error = std::move(error);
                                              answered     = true;
                                          },
                                          [&](Json data) { answer.chunks.push_back(std::move(data)); }});
        REQUIRE(PumpUntil([&] { return answered; }));
        return answer;
    }
};

std::filesystem::path FreshDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void RequireSameMatches(const std::vector<SearchMatch>& actual, const std::vector<SearchMatch>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        REQUIRE(actual[i].file == expected[i].file);
        REQUIRE(actual[i].lineNumber == expected[i].lineNumber);
        REQUIRE(actual[i].lineText == expected[i].lineText);
    }
}

} // namespace

TEST_CASE("ned-agent answers hello with its own build and settles the codec", "[Host][HostAgent]") {
    AgentClient client(FrameTransport({AgentBinary(), "--stdio"}, true));

    const auto answer = client.Call(std::string(host::kHello),
                                    host::ToJson(host::HelloRequest{.codecs = {"zstd", "none"}, .window = 16}));

    REQUIRE(answer.result.has_value());
    const host::HelloResponse hello = host::HelloResponseFromJson(*answer.result);
    REQUIRE(hello.major == host::kProtocolMajor);
    REQUIRE(hello.build == host::FileBuildId(AgentBinary()));
    REQUIRE(hello.arch == host::BuildArch());
    REQUIRE(hello.codec == "zstd");
    REQUIRE(hello.capabilities.search == 1);
    REQUIRE(hello.capabilities.fs == 5);
    REQUIRE(hello.capabilities.watch == 2);
    REQUIRE(hello.capabilities.proc == host::kProcVersion);
}

TEST_CASE("ned-agent's project search matches a local search exactly", "[Host][HostAgent]") {
    const std::filesystem::path dir = FreshDir("ned_host_agent_search");
    {
        std::ofstream many(dir / "many.txt");
        for (int i = 0; i < 3000; ++i) {
            many << "needle " << i << " with some padding to make the frames worth compressing\n";
        }
        std::ofstream(dir / "stale.txt") << "stale needle\n";
        std::ofstream(dir / ".gitignore") << "ignored.txt\n";
        std::ofstream(dir / "ignored.txt") << "needle ignored\n";
        // Enough files for the scan to finish them out of order.
        for (int d = 0; d < 6; ++d) {
            const std::filesystem::path sub = dir / ("d" + std::to_string(d));
            std::filesystem::create_directories(sub);
            for (int f = 0; f < 60; ++f) {
                std::ofstream(sub / ("f" + std::to_string(f) + ".txt")) << "padding\nneedle " << d << ' ' << f << '\n';
            }
        }
    }
    const std::vector<LiveContent> live{
        LiveContent{dir / "stale.txt", std::filesystem::weakly_canonical(dir / "stale.txt"), "fresh needle\n", std::nullopt},
        LiveContent{dir / "unsaved.txt", std::filesystem::weakly_canonical(dir / "unsaved.txt"), "unsaved needle\n", std::nullopt}};
    const std::vector<SearchMatch> local = ned::editor::SearchDirectory(dir, SearchPattern("needle"), live);

    AgentClient client(FrameTransport({AgentBinary(), "--stdio"}, true));
    client.Call(std::string(host::kHello), host::ToJson(host::HelloRequest{.codecs = {"zstd"}, .window = 16}));
    client.connection.Transport().SetCompression(true);

    const auto answer = client.Call(std::string(host::kSearchProject),
                                    host::ToJson(host::SearchRequest{.root = dir, .pattern = "needle", .live = live, .options = {.threads = 8}}));

    REQUIRE(answer.result == Json::object());
    REQUIRE(answer.chunks.size() > 1);
    std::vector<SearchMatch> remote;
    for (const Json& chunk : answer.chunks) {
        for (SearchMatch& match : host::MatchesFromHost(chunk, dir)) {
            remote.push_back(std::move(match));
        }
    }
    REQUIRE(local.size() == 3002 + 6 * 60);
    RequireSameMatches(remote, local);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ned-agent searches under the request's settings rather than its own", "[Host][HostAgent]") {
    const std::filesystem::path dir = FreshDir("ned_host_agent_search_settings");
    std::ofstream(dir / ".editorconfig", std::ios::binary) << "root = true\n[*.txt]\ncharset = latin1\n";
    std::ofstream(dir / "old.txt", std::ios::binary) << "caf\xE9 needle\n";

    AgentClient client(FrameTransport({AgentBinary(), "--stdio"}, true));
    const auto  count = [&](bool editorConfig) {
        const auto answer = client.Call(std::string(host::kSearchProject),
                                         host::ToJson(host::SearchRequest{.root    = dir,
                                                                          .pattern = "caf\xC3\xA9 needle",
                                                                          .live    = {},
                                                                          .options = {.threads = 1, .editorConfig = editorConfig}}));
        REQUIRE(answer.result == Json::object());
        std::size_t matches = 0;
        for (const Json& chunk : answer.chunks) {
            matches += host::MatchesFromHost(chunk, dir).size();
        }
        return matches;
    };
    REQUIRE(count(true) == 1);
    REQUIRE(count(false) == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ned-agent runs through an ssh master's session", "[Host][HostAgent]") {
    const std::filesystem::path dir = FreshDir("ned_host_agent_ssh");
    std::ofstream(dir / "a.txt") << "needle\n";

    ned::editor::remote::SshMaster master("host", {.program = NED_FAKE_SSH}, dir / "m");
    master.Start();
    AgentClient client(FrameTransport(master.SessionArgv(std::vector<std::string>{AgentBinary(), "--stdio"}), true));

    const auto hello = client.Call(std::string(host::kHello), host::ToJson(host::HelloRequest{.codecs = {"none"}, .window = 16}));
    REQUIRE(host::HelloResponseFromJson(*hello.result).build == host::FileBuildId(AgentBinary()));

    const auto search = client.Call(std::string(host::kSearchProject), host::ToJson(host::SearchRequest{.root = dir, .pattern = "needle", .live = {}}));
    REQUIRE(search.result == Json::object());
    REQUIRE(search.chunks.size() == 1);
    REQUIRE(host::MatchesFromHost(search.chunks.front(), dir).size() == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("A deployed agent says hello with the build it was deployed as", "[Host][HostAgent]") {
    namespace remote                        = ned::editor::remote;
    const std::filesystem::path         dir = FreshDir("ned_host_agent_deploy");
    const ned::tests::ScopedEnvOverride data("XDG_DATA_HOME", (dir / "data").c_str());
    const std::filesystem::path         local = dir / "agents" / host::BuildArch() / "ned-agent";
    std::filesystem::create_directories(local.parent_path());
    std::filesystem::copy_file(AgentBinary(), local);
    const remote::AgentDirCandidates candidates{.environment = dir / "agents", .executableRelative = std::nullopt, .installed = "/nonexistent"};

    remote::SshMaster master("host", {.program = NED_FAKE_SSH}, dir / "m");
    master.Start();
    const auto helloBuild = [&](const std::filesystem::path& agent) {
        AgentClient client(FrameTransport(master.SessionArgv(std::vector<std::string>{agent.string(), "--stdio"}), true));
        const auto  answer = client.Call(std::string(host::kHello), host::ToJson(host::HelloRequest{.codecs = {"none"}, .window = 16}));
        return host::HelloResponseFromJson(*answer.result).build;
    };

    const remote::DeployedAgent deployed = remote::EnsureAgent(master, candidates);
    REQUIRE(deployed.uploaded);
    REQUIRE(helloBuild(deployed.remotePath) == deployed.build);

    // A damaged install answers with another build, and a reinstall fixes it.
    std::ofstream(deployed.remotePath, std::ios::binary | std::ios::app) << "damage";
    REQUIRE(helloBuild(deployed.remotePath) != deployed.build);
    REQUIRE(remote::EnsureAgent(master, candidates, true).uploaded);
    REQUIRE(helloBuild(deployed.remotePath) == deployed.build);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ned-agent rejects bad search params and unknown methods", "[Host][HostAgent]") {
    AgentClient client(FrameTransport({AgentBinary(), "--stdio"}, true));

    const auto badPattern = client.Call(std::string(host::kSearchProject),
                                        host::ToJson(host::SearchRequest{.root = "/tmp", .pattern = "(unclosed", .live = {}}));
    REQUIRE(badPattern.error->code == error_code::kInvalidParams);

    const auto badParams = client.Call(std::string(host::kSearchProject), Json{{"root", "/not/bytes"}});
    REQUIRE(badParams.error->code == error_code::kInvalidParams);

    const auto unknown = client.Call("proc/teleport", Json::object());
    REQUIRE(unknown.error->code == error_code::kMethodNotFound);
}

TEST_CASE("ned-agent reads and writes files, streaming a read under credit", "[Host][HostAgent]") {
    const std::filesystem::path dir = FreshDir("ned_host_agent_fs");
    const std::filesystem::path file = dir / "data.bin";
    AgentClient                 client(FrameTransport({AgentBinary(), "--stdio"}, true));

    // More chunks than one credit window holds.
    std::string bytes(6 * 1024 * 1024 + 7, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>((i * 2654435761U) >> 13);
    }
    const auto written = client.Call(std::string(host::kFsWrite),
                                     host::ToJson(host::FsWriteRequest{.path = file, .data = bytes, .expect = std::optional<std::filesystem::file_time_type>()}));
    REQUIRE(written.result.has_value());
    const host::FileStat stat = host::FileStatFromJson(*written.result);
    CHECK(stat.size == bytes.size());
    CHECK(stat.timestamp == std::filesystem::last_write_time(file));
    CHECK(host::FileStatFromJson(*client.Call(std::string(host::kFsStat), host::PathParams(file)).result) == stat);

    const auto read = client.Call(std::string(host::kFsRead), host::PathParams(file));
    REQUIRE(read.result.has_value());
    CHECK(read.chunks.size() == (bytes.size() + host::kFsChunkBytes - 1) / host::kFsChunkBytes);
    std::string received;
    for (const Json& chunk : read.chunks) {
        REQUIRE(chunk.is_binary());
        received.append(chunk.get_binary().begin(), chunk.get_binary().end());
    }
    CHECK(received == bytes);
    CHECK(host::FileStatFromJson(*read.result).timestamp == stat.timestamp);

    // Changed under the editor: refused, and the file kept.
    std::filesystem::last_write_time(file, *stat.timestamp + std::chrono::seconds(3));
    const auto stale = client.Call(std::string(host::kFsWrite), host::ToJson(host::FsWriteRequest{.path = file, .data = "mine", .expect = stat.timestamp}));
    REQUIRE(stale.error.has_value());
    CHECK(stale.error->code == host::fs_error::kFileChanged);
    CHECK(std::filesystem::file_size(file) == bytes.size());

    CHECK(client.Call(std::string(host::kFsRead), host::PathParams(dir / "missing")).error->code == host::fs_error::kNotFound);
    CHECK(client.Call(std::string(host::kFsRead), Json{{"path", "/not/bytes"}}).error->code == error_code::kInvalidParams);
    CHECK(client.Call(std::string(host::kFsWrite), Json{{"path", host::Bytes("/x")}}).error->code == error_code::kInvalidParams);
    std::filesystem::remove_all(dir);
}

TEST_CASE("ned-agent lists a directory in chunks and resolves a path", "[Host][HostAgent]") {
    const std::filesystem::path dir = FreshDir("ned_host_agent_readdir");
    for (std::size_t i = 0; i < host::kFsReaddirChunk + 5; ++i) {
        std::ofstream(dir / ("f" + std::to_string(i)));
    }
    std::filesystem::create_directory(dir / "sub");

    AgentClient client(FrameTransport({AgentBinary(), "--stdio"}, true));
    const auto  listed = client.Call(std::string(host::kFsReaddir), host::PathParams(dir));
    REQUIRE(listed.result == Json::object());
    REQUIRE(listed.chunks.size() == 2);
    std::vector<host::DirEntry> entries;
    for (const Json& chunk : listed.chunks) {
        std::ranges::move(host::DirEntriesFromJson(chunk), std::back_inserter(entries));
    }
    CHECK(entries.size() == host::kFsReaddirChunk + 6);
    CHECK(std::ranges::count(entries, host::FileKind::Directory, &host::DirEntry::kind) == 1);

    const auto missing = client.Call(std::string(host::kFsReaddir), host::PathParams(dir / "nope"));
    REQUIRE(missing.error.has_value());
    CHECK(missing.error->code == host::fs_error::kNotFound);

    const auto home = client.Call(std::string(host::kFsRealpath), Json{{"path", host::Bytes(".")}});
    REQUIRE(home.result.has_value());
    CHECK(host::RequirePath(*home.result, "path") == std::filesystem::current_path());

    std::filesystem::remove_all(dir);
}

TEST_CASE("ned-agent makes, renames and removes paths", "[Host][HostAgent]") {
    const std::filesystem::path dir = FreshDir("ned_host_agent_fileops");
    AgentClient                 client(FrameTransport({AgentBinary(), "--stdio"}, true));

    const auto made = client.Call(std::string(host::kFsMkdir), host::PathParams(dir / "a" / "b"));
    REQUIRE(made.result.has_value());
    CHECK(host::FileStatFromJson(*made.result).kind == host::FileKind::Directory);

    const auto renamed = client.Call(std::string(host::kFsRename), host::ToJson(host::FsRenameRequest{.from = dir / "a", .to = dir / "c"}));
    REQUIRE(renamed.result == Json::object());
    CHECK(std::filesystem::is_directory(dir / "c" / "b"));

    const auto clash = client.Call(std::string(host::kFsRename), host::ToJson(host::FsRenameRequest{.from = dir / "c" / "b", .to = dir / "c"}));
    REQUIRE(clash.error.has_value());
    CHECK(clash.error->code == host::fs_error::kFailed);

    const auto removed = client.Call(std::string(host::kFsRemove), host::PathParams(dir / "c"));
    REQUIRE(removed.result == Json::object());
    CHECK(std::filesystem::is_empty(dir));

    const auto missing = client.Call(std::string(host::kFsRemove), host::PathParams(dir / "c"));
    REQUIRE(missing.error.has_value());
    CHECK(missing.error->code == host::fs_error::kNotFound);

    std::filesystem::remove_all(dir);
}

TEST_CASE("ned-agent reports changes to watched files and directory entries, named as they were asked for", "[Host][HostAgent]") {
    const std::filesystem::path dir = FreshDir("ned_host_agent_watch");
    std::filesystem::create_directories(dir / "sub");
    std::filesystem::create_directory_symlink(dir / "sub", dir / "link");
    std::ofstream(dir / "a.txt") << "a\n";
    AgentClient client(FrameTransport({AgentBinary(), "--stdio"}, true));

    std::vector<ned::editor::FileEvent> events;
    std::vector<ned::editor::FileMove>  moves;
    bool                                overflow = false;
    client.endpoint.HandleNotification(std::string(host::kWatchEvent), [&](const Json& params) {
        const host::WatchEvents batch = host::WatchEventsFromJson(params);
        events.insert(events.end(), batch.events.begin(), batch.events.end());
        moves.insert(moves.end(), batch.moves.begin(), batch.moves.end());
        overflow = overflow || batch.overflow;
    });
    const auto has = [&](const std::filesystem::path& path) {
        return std::ranges::any_of(events, [&](const ned::editor::FileEvent& event) { return event.path == path; });
    };

    const auto set = client.Call(std::string(host::kWatchSet), host::ToJson(host::WatchSetRequest{.files = {dir / "a.txt"}, .directories = {dir / "link"}}));
    REQUIRE(set.result == Json::object());

    // A watched file's sibling stays quiet; an entry in a watched directory
    // is named through the link it was asked for by.
    std::ofstream(dir / "b.txt") << "b\n";
    std::ofstream(dir / "a.txt") << "changed\n";
    std::ofstream(dir / "sub" / "new.txt") << "new\n";
    REQUIRE(client.PumpUntil([&] { return has(dir / "a.txt") && has(dir / "link" / "new.txt"); }));
    CHECK_FALSE(has(dir / "b.txt"));
    CHECK_FALSE(has(dir / "sub" / "new.txt"));
    CHECK_FALSE(overflow);

    std::filesystem::remove(dir / "sub" / "new.txt");
    events.clear();
    REQUIRE(client.PumpUntil([&] { return has(dir / "link" / "new.txt"); }));
    CHECK(events.back().kind == ned::editor::FileEvent::Kind::Deleted);

    // A rename it sees both halves of is a move, named as asked for.
    std::ofstream(dir / "sub" / "old.txt") << "old\n";
    REQUIRE(client.PumpUntil([&] { return has(dir / "link" / "old.txt"); }));
    std::filesystem::rename(dir / "sub" / "old.txt", dir / "sub" / "renamed.txt");
    REQUIRE(client.PumpUntil([&] {
        return std::ranges::contains(moves, ned::editor::FileMove{.from = dir / "link" / "old.txt", .to = dir / "link" / "renamed.txt"});
    }));

    // A tree reports every directory under it, one made later included.
    std::filesystem::create_directories(dir / "tree" / "deep");
    REQUIRE(client.Call(std::string(host::kWatchSet), host::ToJson(host::WatchSetRequest{.files = {}, .directories = {}, .trees = {dir / "tree"}})).result ==
            Json::object());
    events.clear();
    std::ofstream(dir / "tree" / "deep" / "x.c") << "x\n";
    REQUIRE(client.PumpUntil([&] { return has(dir / "tree" / "deep" / "x.c"); }));
    std::filesystem::create_directories(dir / "tree" / "later");
    REQUIRE(client.PumpUntil([&] { return has(dir / "tree" / "later"); }));
    REQUIRE(client.PumpUntil([&] {
        std::ofstream(dir / "tree" / "later" / "y.c") << "y\n";
        return has(dir / "tree" / "later" / "y.c");
    }));

    // An empty set stops reporting.
    REQUIRE(client.Call(std::string(host::kWatchSet), host::ToJson(host::WatchSetRequest{})).result == Json::object());
    events.clear();
    std::ofstream(dir / "a.txt") << "again\n";
    const auto quietUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
    REQUIRE(client.PumpUntil([&] { return std::chrono::steady_clock::now() > quietUntil; }));
    CHECK(events.empty());

    const auto relative = client.Call(std::string(host::kWatchSet), Json{{"files", Json::array({host::Bytes("a.txt")})}, {"directories", Json::array()}});
    REQUIRE(relative.error.has_value());
    CHECK(relative.error->code == error_code::kInvalidParams);

    std::filesystem::remove_all(dir);
}

namespace {

// A proc/spawn in flight: its output so far and, once it ends, its exit.
struct Spawned {
    std::string                       out;
    std::string                       err;
    std::optional<std::optional<int>> exit;
    std::optional<RemoteError>        error;
    std::uint64_t                     id = 0;

    void Start(AgentClient& client, const host::ProcSpawnRequest& request) {
        id = client.endpoint.Request(std::string(host::kProcSpawn), host::ToJson(request),
                                     ResponseHandlers{[this](const Json& result) { exit = host::ProcExitFromJson(result); },
                                                      [this](RemoteError e) { error = std::move(e); },
                                                      [this](const Json& data) {
                                                          const host::ProcOutput output = host::ProcOutputFromJson(data);
                                                          (output.fd == 1 ? out : err) += output.data;
                                                      }});
    }
    [[nodiscard]] bool Done() const {
        return exit.has_value() || error.has_value();
    }
};

host::ProcSpawnRequest Spawn(std::uint64_t handle, std::vector<std::string> argv,
                             ned::editor::process::StderrMode stderrMode = ned::editor::process::StderrMode::Discard) {
    return host::ProcSpawnRequest{.handle = handle, .argv = std::move(argv), .cwd = std::nullopt, .env = {}, .stderrMode = stderrMode};
}

} // namespace

TEST_CASE("ned-agent runs a process, feeding its stdin and streaming its output and exit", "[Host][HostAgent]") {
    AgentClient client(FrameTransport({AgentBinary(), "--stdio"}, true));

    Spawned cat;
    cat.Start(client, Spawn(1, {"sh", "-c", "cat; echo oops >&2; exit 4"}, ned::editor::process::StderrMode::Capture));
    REQUIRE(client.Call(std::string(host::kProcWrite), host::ToJson(host::ProcWriteRequest{.handle = 1, .data = "hello "})).result);
    REQUIRE(client.Call(std::string(host::kProcWrite), host::ToJson(host::ProcWriteRequest{.handle = 1, .data = "there"})).result);
    REQUIRE(client.Call(std::string(host::kProcCloseStdin), host::ProcHandleParams(1)).result);
    REQUIRE(client.PumpUntil([&] { return cat.Done(); }));
    CHECK(cat.out == "hello there");
    CHECK(cat.err == "oops\n");
    CHECK(cat.exit == std::optional<int>(4));

    // Gone once it has ended.
    const auto late = client.Call(std::string(host::kProcWrite), host::ToJson(host::ProcWriteRequest{.handle = 1, .data = "x"}));
    REQUIRE(late.error);
    CHECK(late.error->code == host::proc_error::kNoSuchProcess);

    SECTION("in its directory, with its environment") {
        const std::filesystem::path dir = std::filesystem::canonical(std::filesystem::temp_directory_path());
        Spawned                     env;
        host::ProcSpawnRequest      request = Spawn(2, {"sh", "-c", "pwd; echo \"$NED_PROC_TEST\""});
        request.cwd                         = dir;
        request.env                         = {{"NED_PROC_TEST", "yes"}};
        env.Start(client, request);
        REQUIRE(client.PumpUntil([&] { return env.Done(); }));
        CHECK(env.out == dir.string() + "\nyes\n");
        CHECK(env.exit == std::optional<int>(0));
    }
    SECTION("all of a flood, paced by credit") {
        Spawned flood;
        flood.Start(client, Spawn(2, {"head", "-c", "4000000", "/dev/zero"}));
        REQUIRE(client.PumpUntil([&] { return flood.Done(); }));
        CHECK(flood.out.size() == 4000000);
        CHECK(flood.exit == std::optional<int>(0));
    }
    SECTION("signalled, cancelled or released, it ends") {
        Spawned trapped;
        trapped.Start(client, Spawn(2, {"sh", "-c", "trap 'echo term; exit 3' TERM; echo ready; while :; do sleep 0.05; done"}));
        REQUIRE(client.PumpUntil([&] { return trapped.out == "ready\n"; }));
        REQUIRE(client.Call(std::string(host::kProcSignal), host::ToJson(host::ProcSignalRequest{.handle = 2, .signal = SIGTERM})).result);
        REQUIRE(client.PumpUntil([&] { return trapped.Done(); }));
        CHECK(trapped.out == "ready\nterm\n");
        CHECK(trapped.exit == std::optional<int>(3));

        Spawned cancelled;
        cancelled.Start(client, Spawn(3, {"sleep", "100"}));
        client.endpoint.Cancel(cancelled.id);
        REQUIRE(client.PumpUntil([&] { return cancelled.Done(); }));

        // Released, a child that ignores its stdin's EOF is killed after
        // the grace period.
        Spawned released;
        released.Start(client, Spawn(4, {"sleep", "100"}));
        REQUIRE(client.Call(std::string(host::kProcRelease), host::ProcHandleParams(4)).result);
        REQUIRE(client.PumpUntil([&] { return released.Done(); }));
        CHECK(released.exit == std::optional<std::optional<int>>(std::optional<int>()));
    }
    SECTION("on a pty, which it can resize and which hangs up when released") {
        Spawned                shell;
        host::ProcSpawnRequest request = Spawn(2, {"sh", "-c", "tty; stty size; echo \"$TERM\"; stty -echo; echo ready; read line; stty size"},
                                               ned::editor::process::StderrMode::MergeWithStdout);
        request.env                    = {{"TERM", "xterm-256color"}};
        request.pty                    = ned::editor::process::TerminalSize{.rows = 30, .cols = 100};
        shell.Start(client, request);
        REQUIRE(client.PumpUntil([&] { return shell.out.find("ready") != std::string::npos || shell.Done(); }));
        CHECK_THAT(shell.out, ContainsSubstring("/dev/pts/"));
        CHECK_THAT(shell.out, ContainsSubstring("30 100\r\nxterm-256color\r\n"));
        const host::ProcResizeRequest resize{.handle = 2, .size = {.rows = 40, .cols = 120}};
        REQUIRE(client.Call(std::string(host::kProcResize), host::ToJson(resize)).result == Json::object());
        REQUIRE(client.Call(std::string(host::kProcWrite), host::ToJson(host::ProcWriteRequest{.handle = 2, .data = "\n"})).result);
        REQUIRE(client.PumpUntil([&] { return shell.Done(); }));
        CHECK_THAT(shell.out, ContainsSubstring("ready\r\n40 120\r\n"));
        CHECK(shell.exit == std::optional<int>(0));

        // A shell ignores its stdin's EOF; the hang-up ends it.
        Spawned waiting;
        request.handle = 3;
        request.argv   = {"sh", "-c", "trap 'echo hup; exit 5' HUP; echo ready; while :; do sleep 0.05; done"};
        waiting.Start(client, request);
        REQUIRE(client.PumpUntil([&] { return waiting.out.find("ready") != std::string::npos || waiting.Done(); }));
        REQUIRE(client.Call(std::string(host::kProcRelease), host::ProcHandleParams(3)).result);
        REQUIRE(client.PumpUntil([&] { return waiting.Done(); }));
        CHECK(waiting.exit == std::optional<std::optional<int>>(5));
    }
    SECTION("a command that can't start, or a handle in use, is refused") {
        Spawned missing;
        missing.Start(client, Spawn(2, {"ned-no-such-command"}));
        REQUIRE(client.PumpUntil([&] { return missing.Done(); }));
        REQUIRE(missing.error);
        CHECK(missing.error->code == host::proc_error::kSpawnFailed);

        Spawned first;
        Spawned second;
        first.Start(client, Spawn(3, {"sleep", "100"}));
        second.Start(client, Spawn(3, {"true"}));
        REQUIRE(client.PumpUntil([&] { return second.Done(); }));
        REQUIRE(second.error);
        CHECK(second.error->code == error_code::kInvalidParams);
        REQUIRE(client.Call(std::string(host::kProcSignal), host::ToJson(host::ProcSignalRequest{.handle = 3, .signal = SIGKILL})).result);
        REQUIRE(client.PumpUntil([&] { return first.Done(); }));
        CHECK(first.exit == std::optional<std::optional<int>>(std::optional<int>()));
    }
}

TEST_CASE("RunAgent ends quietly when the editor hangs up and with a reason on a protocol error", "[Host][HostAgent]") {
    const auto run = [](const std::string& bytesThenHangUp) {
        int toAgent[2];
        int fromAgent[2];
        REQUIRE(::pipe(toAgent) == 0);
        REQUIRE(::pipe(fromAgent) == 0);
        std::optional<std::string> failure;
        std::jthread               agent([&] { failure = host::RunAgent(toAgent[0], fromAgent[1], "test"); });
        REQUIRE(::write(toAgent[1], bytesThenHangUp.data(), bytesThenHangUp.size()) == static_cast<ssize_t>(bytesThenHangUp.size()));
        ::close(toAgent[1]);
        agent.join();
        ::close(fromAgent[0]);
        return failure;
    };

    REQUIRE_FALSE(run("").has_value());
    REQUIRE_FALSE(run(EncodeFrame(EncodeCbor(Json{{"method", "note"}, {"params", nullptr}}), false)).has_value());
    REQUIRE(run(EncodeFrame("\xff\xff", false))->starts_with("ned: malformed CBOR"));
    REQUIRE(run(EncodeFrame(EncodeCbor(Json::array({1, 2})), false)) == "ned: protocol message is not a map");
}
