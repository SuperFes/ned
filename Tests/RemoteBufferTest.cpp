#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include "Editor/BufferSave.h"
#include "Editor/Host/FsCodec.h"
#include "Editor/Protocol/Endpoint.h"
#include "Editor/Remote/RemoteBuffer.h"
#include "Editor/Session.h"
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

TEST_CASE("A remote read restores the file's saved place", "[Remote][RemoteBuffer][Session]") {
    struct PlacesGuard {
        PlacesGuard() {
            ned::editor::ResetFilePlacesForTesting();
        }
        ~PlacesGuard() {
            ned::editor::ResetFilePlacesForTesting();
        }
    } guard;
    const ned::text::RemoteFile file{.destination = "host", .path = "/srv/notes.txt"};
    ned::text::Buffer           before = ned::text::Buffer::FromRemoteContents(file, FileContents{.bytes = "one\ntwo\nthree\n", .timestamp = std::nullopt});
    before.SetPoint(before.ByteOffsetForLineAndColumn(2, 3));
    ned::editor::RecordFilePlace(before, std::nullopt, 4);

    ned::text::BufferList list;
    ned::text::Buffer&    placeholder = list.AddRemotePlaceholder(file);
    REQUIRE_FALSE(remote::FinishRemoteLoad(placeholder, FileContents{.bytes = "one\ntwo\nthree\n", .timestamp = std::nullopt}).has_value());
    CHECK(placeholder.Point() == placeholder.ByteOffsetForLineAndColumn(2, 3));
}

TEST_CASE("A remote save keeps edits made while it was in flight, and a host's refusal asks next time", "[Remote][RemoteBuffer]") {
    ned::text::Buffer buffer = ned::text::Buffer::FromRemoteContents({.destination = "host", .path = "/srv/a.txt"},
                                                                     FileContents{.bytes = "one\n", .timestamp = std::nullopt});
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("two\n");

    ned::text::SavePlan plan = buffer.BeginRemoteSave(false, false);
    CHECK(buffer.IsSaving());
    CHECK(plan.target == "host:/srv/a.txt");
    CHECK(ned::text::EncodeSavePlan(plan) == "one\ntwo\n");
    buffer.InsertAtPoint("three\n"); // while the write is out
    const auto when = std::filesystem::file_time_type::clock::now();
    REQUIRE_FALSE(remote::FinishRemoteSave(buffer, std::move(plan), host::FileStat{.kind = host::FileKind::File, .size = 8, .mode = 0644, .timestamp = when}));
    CHECK_FALSE(buffer.IsSaving());
    CHECK(buffer.RemoteTimestamp() == when);
    CHECK(buffer.Modified()); // "three" was never written
    CHECK_FALSE(buffer.Path().has_value());

    ned::text::SavePlan              second = buffer.BeginRemoteSave(false, false);
    const std::optional<std::string> refused =
        remote::FinishRemoteSave(buffer, std::move(second), std::unexpected(remote::FsFailure{.code = host::fs_error::kFileChanged, .message = "/srv/a.txt changed since it was read"}));
    CHECK(refused == "/srv/a.txt changed since it was read; saving again asks to overwrite it");
    CHECK(buffer.ExternallyModified());
    CHECK(buffer.Modified());
    CHECK_FALSE(buffer.IsSaving());

    ned::text::SavePlan third = buffer.BeginRemoteSave(false, false);
    CHECK(remote::FinishRemoteSave(buffer, std::move(third), std::unexpected(remote::FsFailure{.code = ned::editor::protocol::error_code::kTimedOut, .message = "no answer within 30000 ms"})) ==
          "no answer within 30000 ms");
    CHECK(buffer.ExternallyModified());

    ned::text::SavePlan fourth = buffer.BeginRemoteSave(false, false);
    REQUIRE_FALSE(remote::FinishRemoteSave(buffer, std::move(fourth), host::FileStat{.kind = host::FileKind::File, .size = 14, .mode = 0644, .timestamp = when}));
    CHECK_FALSE(buffer.ExternallyModified());
    CHECK_FALSE(buffer.Modified());

    CHECK_THROWS(ned::text::Buffer("plain").BeginRemoteSave());
}

TEST_CASE("WriteBufferToDisk hands a remote buffer to the remote-save dispatcher, encoded as a local save would be", "[Remote][RemoteBuffer]") {
    using ned::editor::RemoteSaveRequest;
    using ned::editor::SaveDispatch;
    struct DispatcherGuard {
        ~DispatcherGuard() {
            ned::editor::SetRemoteSaveDispatcher(nullptr);
        }
    } guard;

    const auto        when   = std::filesystem::file_time_type::clock::now();
    ned::text::Buffer buffer = ned::text::Buffer::FromRemoteContents({.destination = "host", .path = "/srv/a.txt"},
                                                                     FileContents{.bytes     = "\xEF\xBB\xBF"
                                                                                               "a\r\n",
                                                                                  .timestamp = when});
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("b");

    // Nothing to carry it.
    CHECK_THROWS_WITH(ned::editor::WriteBufferToDisk(buffer, SaveDispatch::Automatic), "ned: no remote hosts to save host:/srv/a.txt through");
    CHECK_FALSE(buffer.IsSaving());

    std::optional<RemoteSaveRequest> taken;
    ned::editor::SetRemoteSaveDispatcher([&](RemoteSaveRequest request) {
        taken.emplace(std::move(request));
        return true;
    });
    // A synchronous caller couldn't report the outcome.
    CHECK_THROWS_WITH(ned::editor::WriteBufferToDisk(buffer), "ned: host:/srv/a.txt is on a remote host; save-buffer saves it");
    CHECK_FALSE(taken.has_value());
    CHECK_FALSE(buffer.IsSaving());

    ned::editor::WriteBufferToDisk(buffer, SaveDispatch::Automatic);
    REQUIRE(taken.has_value());
    CHECK(buffer.IsSaving());
    CHECK(taken->buffer.Is(buffer));
    CHECK(taken->file.ToString() == "host:/srv/a.txt");
    CHECK(taken->bytes == "\xEF\xBB\xBF"
                          "a\r\nb\r\n");
    CHECK(taken->expect == std::optional<std::optional<std::filesystem::file_time_type>>(when));
    REQUIRE_FALSE(remote::FinishRemoteSave(buffer, std::move(taken->plan), host::FileStat{.kind = host::FileKind::File, .size = 8, .mode = 0644, .timestamp = when}));

    // Overwriting sends no expectation.
    buffer.InsertAtPoint("c");
    taken.reset();
    ned::editor::WriteBufferToDisk(buffer, SaveDispatch::Automatic, true);
    REQUIRE(taken.has_value());
    CHECK_FALSE(taken->expect.has_value());
    buffer.AbandonSave();

    // Too large for one request: refused before it leaves.
    buffer.InsertAtPoint(std::string(remote::kMaxWriteBytes, 'x'));
    taken.reset();
    CHECK_THROWS_WITH(ned::editor::WriteBufferToDisk(buffer, SaveDispatch::Automatic), Catch::Matchers::ContainsSubstring("too large to save to its host"));
    CHECK_FALSE(taken.has_value());
    CHECK_FALSE(buffer.IsSaving());
}
