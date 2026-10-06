#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Editor/Host/Codec.h"
#include "Editor/Host/Hello.h"
#include "Editor/Host/SearchCodec.h"
#include "Editor/Protocol/Cbor.h"

using ned::editor::LiveContent;
using ned::editor::SearchMatch;
using ned::editor::protocol::DecodeCbor;
using ned::editor::protocol::EncodeCbor;
using ned::editor::protocol::ProtocolError;
namespace host = ned::editor::host;
using Json     = nlohmann::json;

namespace {

Json OverTheWire(const Json& json) {
    return DecodeCbor(EncodeCbor(json));
}

} // namespace

TEST_CASE("Hello messages round-trip and pick zstd only when offered", "[Host][HostCodec]") {
    const host::HelloRequest request{.codecs = {"none", "zstd"}, .window = 8};
    const host::HelloRequest decoded = host::HelloRequestFromJson(OverTheWire(host::ToJson(request)));
    REQUIRE(decoded.major == host::kProtocolMajor);
    REQUIRE(decoded.codecs == request.codecs);
    REQUIRE(decoded.window == 8);

    REQUIRE(host::ChooseCodec({"none", "zstd"}) == "zstd");
    REQUIRE(host::ChooseCodec({"none"}) == "none");
    REQUIRE(host::ChooseCodec({}) == "none");

    host::HelloResponse response{.build = "def", .arch = "x86_64", .codec = "zstd", .window = 16};
    response.capabilities.search   = 1;
    const host::HelloResponse back = host::HelloResponseFromJson(OverTheWire(host::ToJson(response)));
    REQUIRE(back.arch == "x86_64");
    REQUIRE(back.capabilities == response.capabilities);

    // A capability an older host doesn't know reads as not offered.
    Json older = host::ToJson(response);
    older["capabilities"].erase("watch");
    REQUIRE(host::HelloResponseFromJson(older).capabilities.watch == 0);

    Json broken      = host::ToJson(request);
    broken["window"] = "sixteen";
    REQUIRE_THROWS_AS(host::HelloRequestFromJson(broken), ProtocolError);
    REQUIRE_THROWS_AS(host::HelloRequestFromJson(Json::array()), ProtocolError);
}

TEST_CASE("Search requests carry paths as raw bytes", "[Host][HostCodec]") {
    const std::filesystem::path odd("/root/caf\xe9/n\nl.txt"); // Latin-1 byte and a newline: valid POSIX, not UTF-8
    host::SearchRequest         request{.root = "/root", .pattern = "needle", .live = {}};
    request.live.push_back(LiveContent{odd, odd, std::string("bin\0ary", 7), std::nullopt});
    request.live.push_back(LiveContent{"/root/huge", "/real/huge", {}, std::vector<SearchMatch>{{"/root/huge", 9, "x"}}});

    const host::SearchRequest decoded = host::SearchRequestFromJson(OverTheWire(host::ToJson(request)));

    REQUIRE(decoded.root == "/root");
    REQUIRE(decoded.pattern == "needle");
    REQUIRE(decoded.live.size() == 2);
    REQUIRE(decoded.live[0].path == odd);
    REQUIRE(decoded.live[0].text == std::string("bin\0ary", 7));
    REQUIRE_FALSE(decoded.live[0].preScanned.has_value());
    REQUIRE(decoded.live[1].canonicalPath == "/real/huge");
    REQUIRE(decoded.live[1].preScanned->size() == 1);
    REQUIRE((*decoded.live[1].preScanned)[0].lineNumber == 9);
}

TEST_CASE("Search requests carry the editor's search settings", "[Host][HostCodec]") {
    host::SearchRequest request{.root = "/root", .pattern = "needle", .live = {}, .options = {.threads = 3, .editorConfig = false}};

    const host::SearchRequest decoded = host::SearchRequestFromJson(OverTheWire(host::ToJson(request)));
    REQUIRE(decoded.options.threads == 3);
    REQUIRE_FALSE(decoded.options.editorConfig);

    Json noThreads       = host::ToJson(request);
    noThreads["threads"] = 0;
    REQUIRE_THROWS_AS(host::SearchRequestFromJson(noThreads), ProtocolError);
    Json missing = host::ToJson(request);
    missing.erase("editorconfig");
    REQUIRE_THROWS_AS(host::SearchRequestFromJson(missing), ProtocolError);
}

TEST_CASE("Path decoding rejects relative paths, NULs and non-bytes", "[Host][HostCodec]") {
    REQUIRE(host::RequirePath(Json{{"p", host::Bytes("/a/./b/../c")}}, "p") == "/a/c");
    REQUIRE_THROWS_AS(host::RequirePath(Json{{"p", host::Bytes("a/b")}}, "p"), ProtocolError);
    REQUIRE_THROWS_AS(host::RequirePath(Json{{"p", host::Bytes("")}}, "p"), ProtocolError);
    REQUIRE_THROWS_AS(host::RequirePath(Json{{"p", host::Bytes(std::string("/a\0b", 4))}}, "p"), ProtocolError);
    REQUIRE_THROWS_AS(host::RequirePath(Json{{"p", "/a/b"}}, "p"), ProtocolError); // a text string
}

TEST_CASE("Matches from a host must stay under the search root", "[Host][HostCodec]") {
    const std::vector<SearchMatch> inside{{"/root/a.txt", 1, "x"}, {"/root/sub/b.txt", 2, "y"}};
    REQUIRE(host::MatchesFromHost(OverTheWire(host::MatchesToJson(inside)), "/root").size() == 2);

    for (const char* hostile : {"/etc/passwd", "/root/../etc/passwd", "/rootkit/a.txt", "/root"}) {
        const std::vector<SearchMatch> outside{{hostile, 1, "x"}};
        REQUIRE_THROWS_AS(host::MatchesFromHost(host::MatchesToJson(outside), "/root"), ProtocolError);
    }
}

TEST_CASE("ChunkMatches bounds each chunk by count and by bytes", "[Host][HostCodec]") {
    const std::vector<SearchMatch> many(2500, SearchMatch{"/r/a", 1, "short"});
    const auto                     byCount = host::ChunkMatches(many);
    REQUIRE(byCount.size() == 3);
    REQUIRE(byCount[0].size() == 1024);
    REQUIRE(byCount[2].size() == 2500 - 2048);

    const std::vector<SearchMatch> wide(4, SearchMatch{"/r/a", 1, std::string(700 * 1024, 'x')});
    const auto                     byBytes = host::ChunkMatches(wide);
    REQUIRE(byBytes.size() == 2);
    REQUIRE(byBytes[0].size() == 2);

    REQUIRE(host::ChunkMatches({}).empty());
}

TEST_CASE("FileBuildId tells file contents apart", "[Host][HostCodec]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_host_build_id";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "a") << "agent one";
    std::ofstream(dir / "b") << "agent one";
    std::ofstream(dir / "c") << "agent two";

    REQUIRE(host::FileBuildId(dir / "a") == host::FileBuildId(dir / "b"));
    REQUIRE(host::FileBuildId(dir / "a") != host::FileBuildId(dir / "c"));
    REQUIRE(host::FileBuildId(dir / "a").size() == 16);
    REQUIRE_THROWS(host::FileBuildId(dir / "missing"));

    std::filesystem::remove_all(dir);
}
