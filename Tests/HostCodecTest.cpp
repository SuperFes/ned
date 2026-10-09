#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Host/Codec.h"
#include "Editor/Host/FsCodec.h"
#include "Editor/Host/Hello.h"
#include "Editor/Host/ProcCodec.h"
#include "Editor/Host/SearchCodec.h"
#include "Editor/Host/WatchCodec.h"
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

TEST_CASE("fs stats and writes round-trip, timestamps to the nanosecond", "[Host][HostCodec]") {
    using std::chrono::file_clock;
    const auto now = file_clock::now();
    for (const host::FileStat& stat : {host::FileStat{.kind = host::FileKind::File, .size = 12, .mode = 0755, .timestamp = now},
                                       host::FileStat{.kind = host::FileKind::Directory, .size = 0, .mode = 0700, .timestamp = std::nullopt},
                                       host::FileStat{},
                                       host::FileStat{.kind = host::FileKind::Other, .size = 0, .mode = 0, .timestamp = file_clock::time_point{}}}) {
        CHECK(host::FileStatFromJson(OverTheWire(host::ToJson(stat))) == stat);
    }

    // Before 1970 still counts.
    const auto old = std::chrono::clock_cast<file_clock>(std::chrono::sys_days{std::chrono::year{1969} / 7 / 20});
    CHECK(host::FileStatFromJson(OverTheWire(host::ToJson(host::FileStat{.kind = host::FileKind::File, .size = 1, .mode = 0644, .timestamp = old}))).timestamp == old);

    using Expect = std::optional<std::optional<std::filesystem::file_time_type>>;
    for (const Expect& expect : {Expect(), Expect(std::optional<std::filesystem::file_time_type>()), Expect(now)}) {
        const host::FsWriteRequest request{.path = "/srv/caf\xC3\xA9.txt", .data = std::string("a\0b", 3), .expect = expect};
        const host::FsWriteRequest decoded = host::FsWriteRequestFromJson(OverTheWire(host::ToJson(request)));
        CHECK(decoded.path == request.path);
        CHECK(decoded.data == request.data);
        CHECK(decoded.expect == expect);
    }
    CHECK(host::PathParamsFromJson(OverTheWire(host::PathParams("/a/b"))) == "/a/b");
}

TEST_CASE("Malformed fs messages are protocol errors", "[Host][HostCodec]") {
    const Json stat = host::ToJson(host::FileStat{.kind = host::FileKind::File, .size = 1, .mode = 0644, .timestamp = std::nullopt});
    for (const auto& [field, value] : {std::pair<const char*, Json>{"kind", "socket"}, {"kind", 3}, {"size", -1}, {"mode", 010000}, {"mtime", "yesterday"}}) {
        Json bad   = stat;
        bad[field] = value;
        CAPTURE(field);
        CHECK_THROWS_AS(host::FileStatFromJson(bad), ProtocolError);
    }
    for (const char* field : {"kind", "size", "mode", "mtime"}) {
        Json missing = stat;
        missing.erase(field);
        CAPTURE(field);
        CHECK_THROWS_AS(host::FileStatFromJson(missing), ProtocolError);
    }
    CHECK_THROWS_AS(host::FileStatFromJson(Json::array()), ProtocolError);
    CHECK_THROWS_AS(host::FsWriteRequestFromJson(Json{{"path", host::Bytes("/a")}, {"data", "text, not bytes"}}), ProtocolError);
    CHECK_THROWS_AS(host::FsWriteRequestFromJson(Json{{"path", host::Bytes("/a")}, {"data", host::Bytes("x")}, {"expect", 1.5}}), ProtocolError);
    CHECK_THROWS_AS(host::PathParamsFromJson(Json{{"path", host::Bytes("relative")}}), ProtocolError);
}

TEST_CASE("Directory entries round-trip, and one that isn't a plain file name is a protocol error", "[Host][HostCodec]") {
    const std::vector<host::DirEntry> entries{{.name = "a.txt", .kind = host::FileKind::File, .link = false},
                                              {.name = "caf\xC3\xA9", .kind = host::FileKind::Directory, .link = true},
                                              {.name = "gone", .kind = host::FileKind::Missing, .link = true}};
    CHECK(host::DirEntriesFromJson(OverTheWire(host::DirEntriesToJson(entries))) == entries);

    for (const std::string& name : std::vector<std::string>{"", ".", "..", "a/b", std::string("a\0b", 3)}) {
        CAPTURE(name);
        CHECK_THROWS_AS(host::DirEntriesFromJson(Json::array({Json{{"name", host::Bytes(name)}, {"kind", "file"}, {"link", false}}})), ProtocolError);
    }
    CHECK_THROWS_AS(host::DirEntriesFromJson(Json::array({Json{{"name", host::Bytes("a")}, {"kind", "file"}}})), ProtocolError);
    CHECK_THROWS_AS(host::DirEntriesFromJson(Json::object()), ProtocolError);

    CHECK(host::RealpathParamsFromJson(Json{{"path", host::Bytes("")}}) == ".");
    CHECK(host::RealpathParamsFromJson(Json{{"path", host::Bytes("src/a")}}) == "src/a");
    CHECK_THROWS_AS(host::RealpathParamsFromJson(Json{{"path", host::Bytes(std::string("a\0b", 3))}}), ProtocolError);
}

TEST_CASE("Rename requests round-trip and need both absolute paths", "[Host][HostCodec]") {
    const host::FsRenameRequest request{.from = "/a/caf\xC3\xA9", .to = "/b/c"};
    CHECK(host::FsRenameRequestFromJson(OverTheWire(host::ToJson(request))) == request);
    CHECK_THROWS_AS(host::FsRenameRequestFromJson(Json{{"from", host::Bytes("/a")}}), ProtocolError);
    CHECK_THROWS_AS(host::FsRenameRequestFromJson(Json{{"from", host::Bytes("/a")}, {"to", host::Bytes("b")}}), ProtocolError);
}

TEST_CASE("Project root requests round-trip and need an absolute path", "[Host][HostCodec]") {
    for (const bool detect : {true, false}) {
        const host::FsProjectRootRequest request{.path = "/a/caf\xC3\xA9", .detect = detect};
        CHECK(host::FsProjectRootRequestFromJson(OverTheWire(host::ToJson(request))) == request);
    }
    CHECK_THROWS_AS(host::FsProjectRootRequestFromJson(Json{{"path", host::Bytes("/a")}}), ProtocolError);
    CHECK_THROWS_AS(host::FsProjectRootRequestFromJson(Json{{"path", host::Bytes("a")}, {"detect", true}}), ProtocolError);
}

TEST_CASE("Project root requests carry a language's markers, which must be strings", "[Host][HostCodec]") {
    const host::FsProjectRootRequest request{.path = "/a/b.rs", .detect = true, .markers = {"Cargo.toml", "*.csproj"}};
    CHECK(host::FsProjectRootRequestFromJson(OverTheWire(host::ToJson(request))) == request);
    CHECK_FALSE(host::ToJson(host::FsProjectRootRequest{.path = "/a"}).contains("markers"));
    CHECK_THROWS_AS(host::FsProjectRootRequestFromJson(Json{{"path", host::Bytes("/a")}, {"detect", true}, {"markers", Json::array({1})}}),
                    ProtocolError);
}

TEST_CASE("Watch requests and events round-trip and refuse what they can't name", "[Host][HostCodec]") {
    using Kind = ned::editor::FileEvent::Kind;
    const host::WatchSetRequest request{.files = {"/a/caf\xC3\xA9", "/b"}, .directories = {"/c/d"}, .trees = {"/e"}};
    CHECK(host::WatchSetRequestFromJson(OverTheWire(host::ToJson(request))) == request);
    // An editor before watch capability 2 names no trees.
    CHECK(host::WatchSetRequestFromJson(Json{{"files", Json::array()}, {"directories", Json::array()}}).trees.empty());
    CHECK(host::WatchSetRequestFromJson(OverTheWire(host::ToJson(host::WatchSetRequest{}))) == host::WatchSetRequest{});
    CHECK_THROWS_AS(host::WatchSetRequestFromJson(Json{{"files", Json::array()}}), ProtocolError);
    CHECK_THROWS_AS(host::WatchSetRequestFromJson(Json{{"files", Json::array({host::Bytes("rel")})}, {"directories", Json::array()}}),
                    ProtocolError);

    const host::WatchEvents events{.events   = {{.path = "/a", .kind = Kind::Created}, {.path = "/b", .kind = Kind::Changed}, {.path = "/c", .kind = Kind::Deleted}},
                                   .moves    = {{.from = "/c", .to = "/a"}},
                                   .overflow = true};
    CHECK(host::WatchEventsFromJson(OverTheWire(host::ToJson(events))) == events);
    // An agent before watch capability 2 names no moves.
    CHECK(host::WatchEventsFromJson(Json{{"events", Json::array()}, {"overflow", false}}).moves.empty());
    CHECK_THROWS_AS(host::WatchEventsFromJson(Json{{"events", Json::array({Json{{"path", host::Bytes("/a")}, {"kind", "moved"}}})}, {"overflow", false}}),
                    ProtocolError);
    CHECK_THROWS_AS(host::WatchEventsFromJson(Json{{"events", Json::array({Json{{"path", host::Bytes("a")}, {"kind", "created"}}})}, {"overflow", false}}),
                    ProtocolError);
    CHECK_THROWS_AS(host::WatchEventsFromJson(Json{{"events", Json::array()}}), ProtocolError);
}

TEST_CASE("Proc requests and output round-trip and refuse what they can't carry", "[Host][HostCodec]") {
    const host::ProcSpawnRequest spawn{.handle     = 7,
                                       .argv       = {"sh", "-c", "caf\xC3\xA9"},
                                       .cwd        = "/srv/proj",
                                       .env        = {{"A", "1"}, {"B", std::nullopt}},
                                       .stderrMode = ned::editor::process::StderrMode::Capture};
    CHECK(host::ProcSpawnRequestFromJson(OverTheWire(host::ToJson(spawn))) == spawn);
    host::ProcSpawnRequest plain = spawn;
    plain.cwd                    = std::nullopt;
    plain.env                    = {};
    CHECK(host::ProcSpawnRequestFromJson(OverTheWire(host::ToJson(plain))) == plain);
    host::ProcSpawnRequest onPty = plain;
    onPty.stderrMode             = ned::editor::process::StderrMode::MergeWithStdout;
    onPty.pty                    = ned::editor::process::TerminalSize{.rows = 30, .cols = 100};
    CHECK(host::ProcSpawnRequestFromJson(OverTheWire(host::ToJson(onPty))) == onPty);
    Json beforePtys = host::ToJson(plain);
    beforePtys.erase("pty");
    CHECK(host::ProcSpawnRequestFromJson(beforePtys) == plain);

    Json noCommand      = host::ToJson(spawn);
    noCommand["argv"]   = Json::array();
    Json badName        = host::ToJson(spawn);
    badName["env"]      = Json{{"A=B", host::Bytes("x")}};
    Json badStderr      = host::ToJson(spawn);
    badStderr["stderr"] = "tee";
    Json ptyStderr          = host::ToJson(onPty);
    ptyStderr["stderr"]     = "capture";
    Json emptyPty           = host::ToJson(onPty);
    emptyPty["pty"]["rows"] = 0;
    for (const Json& bad : {noCommand, badName, badStderr, ptyStderr, emptyPty}) {
        CHECK_THROWS_AS(host::ProcSpawnRequestFromJson(bad), ProtocolError);
    }

    const host::ProcOutput output{.fd = 2, .data = std::string("a\0b", 3)};
    CHECK(host::ProcOutputFromJson(OverTheWire(host::ToJson(output))) == output);
    CHECK_THROWS_AS(host::ProcOutputFromJson(Json{{"fd", 3}, {"data", host::Bytes("x")}}), ProtocolError);

    CHECK(host::ProcExitFromJson(OverTheWire(host::ProcExitToJson(3))) == std::optional<int>(3));
    CHECK(host::ProcExitFromJson(OverTheWire(host::ProcExitToJson(std::nullopt))) == std::nullopt);
    CHECK_THROWS_AS(host::ProcExitFromJson(Json{{"exit", 300}}), ProtocolError);

    const host::ProcWriteRequest write{.handle = 7, .data = "in"};
    CHECK(host::ProcWriteRequestFromJson(OverTheWire(host::ToJson(write))) == write);
    const host::ProcSignalRequest signal{.handle = 7, .signal = SIGINT};
    CHECK(host::ProcSignalRequestFromJson(OverTheWire(host::ToJson(signal))) == signal);
    CHECK_THROWS_AS(host::ProcSignalRequestFromJson(Json{{"handle", 7}, {"signal", "usr1"}}), ProtocolError);
    CHECK(host::ProcHandleFromJson(OverTheWire(host::ProcHandleParams(7))) == 7);
    const host::ProcResizeRequest resize{.handle = 7, .size = {.rows = 1, .cols = 0xffff}};
    CHECK(host::ProcResizeRequestFromJson(OverTheWire(host::ToJson(resize))) == resize);
    CHECK_THROWS_AS(host::ProcResizeRequestFromJson(Json{{"handle", 7}, {"rows", 24}, {"cols", 0x10000}}), ProtocolError);
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
