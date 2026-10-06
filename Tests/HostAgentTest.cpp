#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "Editor/Host/Agent.h"
#include "Editor/Host/Hello.h"
#include "Editor/Host/SearchCodec.h"
#include "Editor/PostQueue.h"
#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/Endpoint.h"
#include "Editor/Protocol/FrameTransport.h"
#include "Editor/Protocol/FramedConnection.h"

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
                                    host::ToJson(host::HelloRequest{.build = "editor", .codecs = {"zstd", "none"}, .window = 16}));

    REQUIRE(answer.result.has_value());
    const host::HelloResponse hello = host::HelloResponseFromJson(*answer.result);
    REQUIRE(hello.major == host::kProtocolMajor);
    REQUIRE(hello.build == host::FileBuildId(AgentBinary()));
    REQUIRE(hello.arch == host::BuildArch());
    REQUIRE(hello.codec == "zstd");
    REQUIRE(hello.capabilities.search == 1);
    REQUIRE(hello.capabilities.fs == 0);
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
    client.Call(std::string(host::kHello), host::ToJson(host::HelloRequest{.build = "editor", .codecs = {"zstd"}, .window = 16}));
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

TEST_CASE("ned-agent rejects bad search params and unknown methods", "[Host][HostAgent]") {
    AgentClient client(FrameTransport({AgentBinary(), "--stdio"}, true));

    const auto badPattern = client.Call(std::string(host::kSearchProject),
                                        host::ToJson(host::SearchRequest{.root = "/tmp", .pattern = "(unclosed", .live = {}}));
    REQUIRE(badPattern.error->code == error_code::kInvalidParams);

    const auto badParams = client.Call(std::string(host::kSearchProject), Json{{"root", "/not/bytes"}});
    REQUIRE(badParams.error->code == error_code::kInvalidParams);

    const auto unknown = client.Call("fs/read", Json::object());
    REQUIRE(unknown.error->code == error_code::kMethodNotFound);
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
