#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Editor/Host/FsCodec.h"
#include "Editor/Remote/RemoteBuffer.h"
#include "Text/BufferList.h"

namespace remote = ned::editor::remote;
namespace host   = ned::editor::host;
using ned::text::FileContents;

TEST_CASE("A remote read fills its placeholder, settings and all", "[Remote][RemoteBuffer]") {
    ned::text::BufferList list;
    ned::text::Buffer&    placeholder = list.AddRemotePlaceholder({.destination = "host", .path = "/srv/make.mk"});
    const auto            when        = std::filesystem::file_time_type::clock::now();

    REQUIRE_FALSE(remote::FinishRemoteLoad(placeholder, FileContents{.bytes = "all:\r\n\tcc a.c\r\n\tcc b.c\r\n", .timestamp = when}).has_value());
    CHECK(placeholder.Text() == "all:\n\tcc a.c\n\tcc b.c\n");
    CHECK(placeholder.LineEndingKind() == ned::text::LineEnding::CRLF);
    CHECK(placeholder.RemoteTimestamp() == when);
    CHECK_FALSE(placeholder.IsLoading());
    CHECK_FALSE(placeholder.ReadOnly());
    CHECK_FALSE(placeholder.Modified());
    CHECK(placeholder.LocalIndent().useTabs == true);
}

TEST_CASE("A remote read that fails leaves its placeholder loading, unless the file is just missing", "[Remote][RemoteBuffer]") {
    ned::text::BufferList list;
    ned::text::Buffer&    failed = list.AddRemotePlaceholder({.destination = "host", .path = "/srv/a"});
    CHECK(remote::FinishRemoteLoad(failed, std::unexpected(remote::FsFailure{.code = host::fs_error::kFailed, .message = "/srv/a is a directory"})) ==
          "/srv/a is a directory");
    CHECK(failed.IsLoading());
    CHECK(remote::FinishRemoteLoad(failed, FileContents{.bytes = std::string("\0\1", 2), .timestamp = std::nullopt})->find("binary") != std::string::npos);
    CHECK(failed.IsLoading());

    ned::text::Buffer& missing = list.AddRemotePlaceholder({.destination = "host", .path = "/srv/new"});
    CHECK_FALSE(remote::FinishRemoteLoad(missing, std::unexpected(remote::FsFailure{.code = host::fs_error::kNotFound, .message = "no such file"})).has_value());
    CHECK_FALSE(missing.IsLoading());
    CHECK(missing.Text().empty());
    CHECK_FALSE(missing.RemoteTimestamp().has_value());
}
