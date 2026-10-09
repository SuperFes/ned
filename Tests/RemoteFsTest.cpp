#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Host/Codec.h"
#include "Editor/Host/FsCodec.h"
#include "Editor/PostQueue.h"
#include "Editor/ProcessTimeouts.h"
#include "Editor/Protocol/Endpoint.h"
#include "Editor/Remote/Connection.h"
#include "Editor/Remote/Fs.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteHost.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::PostQueue;
using ned::editor::remote::FsResult;
using ned::tests::PumpUntil;
namespace fs         = std::filesystem;
namespace host       = ned::editor::host;
namespace remote     = ned::editor::remote;
namespace error_code = ned::editor::protocol::error_code;
using Json           = nlohmann::json;

namespace {

template <typename T>
struct Answer {
    std::optional<FsResult<T>> value;
    auto                       Done() {
        return [this](FsResult<T> result) { value = std::move(result); };
    }
};

} // namespace

TEST_CASE("A connected host's files are stat'd, read and written", "[Remote][RemoteFs][HostAgent]") {
    const ned::tests::FakeRemoteHost fake;
    PostQueue                        queue;
    remote::Connection               connection(remote::Connect(fake.Options()), queue);
    const fs::path                   file = fake.dir / "notes.txt";

    Answer<host::FileStat> written;
    remote::WriteFile(connection, host::FsWriteRequest{.path = file, .data = "caf\xC3\xA9\r\n", .expect = std::optional<fs::file_time_type>()}, written.Done());
    REQUIRE(PumpUntil(queue, [&] { return written.value.has_value(); }));
    REQUIRE(written.value->has_value());
    CHECK((*written.value)->size == 7);

    Answer<host::FileStat> stat;
    remote::StatFile(connection, file, stat.Done());
    REQUIRE(PumpUntil(queue, [&] { return stat.value.has_value(); }));
    CHECK(**stat.value == **written.value);

    Answer<ned::text::FileContents> read;
    remote::ReadFile(connection, file, 1024, read.Done());
    REQUIRE(PumpUntil(queue, [&] { return read.value.has_value(); }));
    REQUIRE(read.value->has_value());
    CHECK((*read.value)->bytes == "caf\xC3\xA9\r\n");
    CHECK((*read.value)->timestamp == (*written.value)->timestamp);

    SECTION("a read past its limit fails and is cancelled") {
        Answer<ned::text::FileContents> capped;
        remote::ReadFile(connection, file, 6, capped.Done());
        REQUIRE(PumpUntil(queue, [&] { return capped.value.has_value(); }));
        REQUIRE_FALSE(capped.value->has_value());
        CHECK_THAT(capped.value->error().message, ContainsSubstring("is larger than 6 bytes"));
    }
    SECTION("a write over a changed file is refused") {
        fs::last_write_time(file, *(*written.value)->timestamp + std::chrono::seconds(2));
        Answer<host::FileStat> stale;
        remote::WriteFile(connection, host::FsWriteRequest{.path = file, .data = "mine", .expect = (*written.value)->timestamp}, stale.Done());
        REQUIRE(PumpUntil(queue, [&] { return stale.value.has_value(); }));
        REQUIRE_FALSE(stale.value->has_value());
        CHECK(stale.value->error().code == host::fs_error::kFileChanged);
    }
    SECTION("a missing file") {
        Answer<ned::text::FileContents> missing;
        remote::ReadFile(connection, fake.dir / "none", 1024, missing.Done());
        REQUIRE(PumpUntil(queue, [&] { return missing.value.has_value(); }));
        CHECK(missing.value->error().code == host::fs_error::kNotFound);
    }
    SECTION("a write too large for one request goes up as an upload first") {
        Answer<host::FileStat> huge;
        std::string            content(remote::kMaxWriteBytes + remote::kUploadPieceBytes / 2, 'x');
        content.back() = 'y';
        CHECK(remote::WriteFile(connection, host::FsWriteRequest{.path = file, .data = content, .expect = std::nullopt}, huge.Done()) == 0);
        REQUIRE(PumpUntil(queue, [&] { return huge.value.has_value(); }, std::chrono::seconds(120)));
        REQUIRE(huge.value->has_value());
        CHECK((*huge.value)->size == content.size());
        std::ifstream in(file, std::ios::binary);
        CHECK(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) == content);
        CHECK(connection.Endpoint().PendingRequests() == 0);
    }
    SECTION("a write naming an upload the host doesn't hold is refused") {
        Answer<host::FileStat> missing;
        remote::WriteFile(connection, host::FsWriteRequest{.path = file, .data = {}, .upload = 999999, .expect = std::nullopt}, missing.Done());
        REQUIRE(PumpUntil(queue, [&] { return missing.value.has_value(); }));
        REQUIRE_FALSE(missing.value->has_value());
        CHECK_THAT(missing.value->error().message, ContainsSubstring("no upload 999999"));
    }
}

TEST_CASE("A connected host's tree lists its files as git sees them", "[Remote][RemoteFs][HostAgent]") {
    const ned::tests::FakeRemoteHost fake;
    PostQueue                        queue;
    remote::Connection               connection(remote::Connect(fake.Options()), queue);
    const fs::path                   dir = fake.dir / "tree";
    fs::create_directories(dir / ".git");
    fs::create_directories(dir / "src" / "deep");
    fs::create_directories(dir / "build");
    std::ofstream(dir / ".gitignore") << "build/\n";
    std::ofstream(dir / "src" / "a.c");
    std::ofstream(dir / "src" / "deep" / "b.c");
    std::ofstream(dir / "build" / "out.o");
    std::ofstream(dir / ".git" / "HEAD");

    Answer<std::vector<fs::path>> listed;
    remote::ListTree(connection, dir, 100, listed.Done());
    REQUIRE(PumpUntil(queue, [&] { return listed.value.has_value(); }));
    REQUIRE(listed.value->has_value());
    std::vector<fs::path> files = **listed.value;
    std::ranges::sort(files);
    CHECK(files == std::vector<fs::path>{".gitignore", "src/a.c", "src/deep/b.c"});

    Answer<std::vector<fs::path>> capped;
    remote::ListTree(connection, dir, 2, capped.Done());
    REQUIRE(PumpUntil(queue, [&] { return capped.value.has_value(); }));
    CHECK_FALSE(capped.value->has_value());
}

TEST_CASE("A connected host's directories are listed and its paths resolved", "[Remote][RemoteFs][HostAgent]") {
    const ned::tests::FakeRemoteHost fake;
    PostQueue                        queue;
    remote::Connection               connection(remote::Connect(fake.Options()), queue);
    const fs::path                   dir = fake.dir / "tree";
    fs::create_directories(dir / "sub");
    for (std::size_t i = 0; i < host::kFsReaddirChunk; ++i) {
        std::ofstream(dir / ("f" + std::to_string(i)));
    }

    Answer<std::vector<host::DirEntry>> listed;
    remote::ListDirectory(connection, dir, 5000, listed.Done());
    REQUIRE(PumpUntil(queue, [&] { return listed.value.has_value(); }));
    REQUIRE(listed.value->has_value());
    CHECK((*listed.value)->size() == host::kFsReaddirChunk + 1);
    CHECK(std::ranges::count((**listed.value), host::FileKind::Directory, &host::DirEntry::kind) == 1);

    // Past the limit, in its second chunk.
    Answer<std::vector<host::DirEntry>> capped;
    remote::ListDirectory(connection, dir, host::kFsReaddirChunk, capped.Done());
    REQUIRE(PumpUntil(queue, [&] { return capped.value.has_value(); }));
    REQUIRE_FALSE(capped.value->has_value());
    CHECK_THAT(capped.value->error().message, ContainsSubstring("holds more than 1024 entries"));

    Answer<std::vector<host::DirEntry>> missing;
    remote::ListDirectory(connection, dir / "none", 5000, missing.Done());
    REQUIRE(PumpUntil(queue, [&] { return missing.value.has_value(); }));
    CHECK(missing.value->error().code == host::fs_error::kNotFound);

    Answer<fs::path> resolved;
    remote::ResolvePath(connection, dir / "sub" / ".." / "sub", resolved.Done());
    REQUIRE(PumpUntil(queue, [&] { return resolved.value.has_value(); }));
    REQUIRE(resolved.value->has_value());
    CHECK(**resolved.value == fs::canonical(dir / "sub"));

    Answer<fs::path> gone;
    remote::ResolvePath(connection, dir / "none", gone.Done());
    REQUIRE(PumpUntil(queue, [&] { return gone.value.has_value(); }));
    CHECK(gone.value->error().code == host::fs_error::kNotFound);
}

TEST_CASE("A connected host finds a path's project root", "[Remote][RemoteFs][HostAgent]") {
    const ned::tests::FakeRemoteHost fake;
    PostQueue                        queue;
    remote::Connection               connection(remote::Connect(fake.Options()), queue);
    const fs::path                   project = fake.dir / "project";
    fs::create_directories(project / ".git");
    fs::create_directories(project / "src");
    std::ofstream(project / "src" / "a.c");

    const auto root = [&](const fs::path& path, bool detect) {
        Answer<fs::path> found;
        remote::FindProjectRoot(connection, host::FsProjectRootRequest{.path = path, .detect = detect}, found.Done());
        REQUIRE(PumpUntil(queue, [&] { return found.value.has_value(); }));
        REQUIRE(found.value->has_value());
        return **found.value;
    };
    const fs::path canonical = fs::canonical(project);
    CHECK(root(project / "src" / "a.c", true) == canonical);
    CHECK(root(project / "src" / "new.c", true) == canonical);
    CHECK(root(project / "src" / "a.c", false) == canonical / "src");
    CHECK(root(project / "src", true) == canonical / "src");
}

TEST_CASE("A connected host finds a language's root by its markers before the repository's", "[Remote][RemoteFs][HostAgent]") {
    const ned::tests::FakeRemoteHost fake;
    PostQueue                        queue;
    remote::Connection               connection(remote::Connect(fake.Options()), queue);
    const fs::path                   project = fake.dir / "markers";
    fs::create_directories(project / ".git");
    fs::create_directories(project / "pkg" / "src");
    std::ofstream(project / "pkg" / "Cargo.toml");

    const auto root = [&](bool detect, std::vector<std::string> markers) {
        Answer<fs::path> found;
        remote::FindProjectRoot(connection,
                                host::FsProjectRootRequest{.path = project / "pkg" / "src" / "lib.rs", .detect = detect, .markers = std::move(markers)},
                                found.Done());
        REQUIRE(PumpUntil(queue, [&] { return found.value.has_value(); }));
        REQUIRE(found.value->has_value());
        return **found.value;
    };
    const fs::path canonical = fs::canonical(project);
    CHECK(root(true, {"Cargo.toml"}) == canonical / "pkg");
    CHECK(root(true, {"go.mod"}) == canonical);
    CHECK(root(false, {"Cargo.toml"}) == canonical / "pkg" / "src");
}

TEST_CASE("A connected host's paths are made, renamed and removed", "[Remote][RemoteFs][HostAgent]") {
    const ned::tests::FakeRemoteHost fake;
    PostQueue                        queue;
    remote::Connection               connection(remote::Connect(fake.Options()), queue);
    const fs::path                   dir = fake.dir / "ops";

    Answer<host::FileStat> made;
    remote::MakeDirectory(connection, dir / "a" / "b", made.Done());
    REQUIRE(PumpUntil(queue, [&] { return made.value.has_value(); }));
    REQUIRE(made.value->has_value());
    CHECK((*made.value)->kind == host::FileKind::Directory);

    Answer<void> renamed;
    remote::RenamePath(connection, {.from = dir / "a", .to = dir / "c"}, renamed.Done());
    REQUIRE(PumpUntil(queue, [&] { return renamed.value.has_value(); }));
    REQUIRE(renamed.value->has_value());
    CHECK(fs::is_directory(dir / "c" / "b"));

    Answer<void> clash;
    remote::RenamePath(connection, {.from = dir / "c" / "b", .to = dir / "c"}, clash.Done());
    REQUIRE(PumpUntil(queue, [&] { return clash.value.has_value(); }));
    REQUIRE_FALSE(clash.value->has_value());
    CHECK_THAT(clash.value->error().message, ContainsSubstring("already exists"));

    Answer<void> removed;
    remote::RemovePath(connection, dir / "c", removed.Done());
    REQUIRE(PumpUntil(queue, [&] { return removed.value.has_value(); }));
    REQUIRE(removed.value->has_value());
    CHECK(fs::is_empty(dir));

    Answer<void> gone;
    remote::RemovePath(connection, dir / "c", gone.Done());
    REQUIRE(PumpUntil(queue, [&] { return gone.value.has_value(); }));
    CHECK(gone.value->error().code == host::fs_error::kNotFound);
}

TEST_CASE("A listing answered malformed is cancelled", "[Remote][RemoteFs]") {
    const fs::path       dir = ned::tests::FreshRemoteDir();
    PostQueue            queue;
    ned::tests::PipePeer peer(queue, dir);

    Answer<std::vector<host::DirEntry>> listed;
    const std::uint64_t                 id = remote::ListDirectory(*peer.connection, "/srv", 100, listed.Done());
    CHECK(peer.Receive()["method"] == host::kFsReaddir);
    peer.Send(Json{{"method", "$/chunk"}, {"params", {{"id", id}, {"seq", 0}, {"data", Json::array({Json{{"name", host::Bytes("../up")}, {"kind", "file"}, {"link", false}}})}}}});
    REQUIRE(PumpUntil(queue, [&] { return peer.HasMessage(); }));
    CHECK(peer.Receive() == Json{{"method", "$/cancel"}, {"params", {{"id", id}}}});
    peer.Send(Json{{"id", id}, {"error", {{"code", error_code::kCancelled}, {"message", "cancelled"}}}});
    REQUIRE(PumpUntil(queue, [&] { return listed.value.has_value(); }));
    CHECK_THAT(listed.value->error().message, ContainsSubstring("is not a file name"));
    fs::remove_all(dir);
}

TEST_CASE("A host that goes quiet fails its fs requests, and one answering malformed is cancelled", "[Remote][RemoteFs]") {
    const fs::path       dir = ned::tests::FreshRemoteDir();
    PostQueue            queue;
    ned::tests::PipePeer peer(queue, dir);

    SECTION("quiet") {
        Answer<host::FileStat> stat;
        const std::uint64_t    id = remote::StatFile(*peer.connection, "/srv/a", stat.Done());
        CHECK(peer.Receive()["method"] == host::kFsStat);
        peer.connection->Endpoint().ExpireIdle(std::chrono::steady_clock::now() + ned::editor::ProtocolRequestTimeoutMs());
        REQUIRE(stat.value.has_value());
        CHECK(stat.value->error().code == error_code::kTimedOut);
        CHECK(peer.Receive() == Json{{"method", "$/cancel"}, {"params", {{"id", id}}}});

        // A write's limit allows for its upload.
        CHECK(remote::WriteIdleLimit(0) == ned::editor::ProtocolRequestTimeoutMs());
        CHECK(remote::WriteIdleLimit(std::size_t{48} << 20) == ned::editor::ProtocolRequestTimeoutMs() + std::chrono::seconds(192));
    }
    SECTION("malformed") {
        Answer<ned::text::FileContents> read;
        const std::uint64_t             id = remote::ReadFile(*peer.connection, "/srv/a", 1024, read.Done());
        (void)peer.Receive();
        peer.Send(Json{{"method", "$/chunk"}, {"params", {{"id", id}, {"seq", 0}, {"data", "text, not bytes"}}}});
        REQUIRE(PumpUntil(queue, [&] { return peer.HasMessage(); }));
        CHECK(peer.Receive() == Json{{"method", "$/cancel"}, {"params", {{"id", id}}}});
        peer.Send(Json{{"id", id}, {"error", {{"code", error_code::kCancelled}, {"message", "cancelled"}}}});
        REQUIRE(PumpUntil(queue, [&] { return read.value.has_value(); }));
        CHECK_THAT(read.value->error().message, ContainsSubstring("isn't bytes"));

        Answer<host::FileStat> stat;
        const std::uint64_t    statId = remote::StatFile(*peer.connection, "/srv/a", stat.Done());
        (void)peer.Receive();
        peer.Send(Json{{"id", statId}, {"result", {{"kind", "socket"}}}});
        REQUIRE(PumpUntil(queue, [&] { return stat.value.has_value(); }));
        CHECK_THAT(stat.value->error().message, ContainsSubstring("the host's answer is malformed"));
    }
    fs::remove_all(dir);
}

TEST_CASE("Hosts expires idle requests on every connection", "[Remote][RemoteFs][HostAgent]") {
    const ned::tests::FakeRemoteHost fake;
    PostQueue                        queue;
    remote::Hosts                    hosts(queue, [&fake](const std::string& destination) { return fake.Options(destination); });
    remote::Connection*              connection = nullptr;
    hosts.Connect("host", [&](std::expected<remote::Connection*, std::string> result) { connection = result.value_or(nullptr); });
    REQUIRE(PumpUntil(queue, [&] { return connection != nullptr; }));

    // A request no host answers: one the agent doesn't know, sent with a limit already past.
    std::optional<int> code;
    connection->Endpoint().Request("fs/never", Json::object(),
                                   ned::editor::protocol::ResponseHandlers{{}, [&](const ned::editor::protocol::RemoteError& error) { code = error.code; }, {}},
                                   std::chrono::milliseconds(0));
    hosts.ExpireIdleRequests();
    CHECK(code == error_code::kTimedOut);
    // The agent's own answer arrives later and goes nowhere.
    queue.DrainPosted_();
    CHECK(connection->Connected());
}
