#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Remote/CommandLine.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteHost.h"

using ned::editor::remote::RemoteArgument;
using ned::editor::remote::RemoteArgumentKind;
namespace fs = std::filesystem;

TEST_CASE("A command-line argument names a host's file the way scp reads one", "[Remote][RemoteCommandLine]") {
    CHECK(RemoteArgument("user@host:/dev/dir") == ned::text::RemoteFile{.destination = "user@host", .path = "/dev/dir"});
    CHECK(RemoteArgument("host:/srv/app/") == ned::text::RemoteFile{.destination = "host", .path = "/srv/app"});
    CHECK(RemoteArgument("[::1]:/srv") == ned::text::RemoteFile{.destination = "[::1]", .path = "/srv"});

    CHECK_FALSE(RemoteArgument("notes.txt").has_value());
    CHECK_FALSE(RemoteArgument("/srv/app").has_value());
    CHECK_FALSE(RemoteArgument("host:relative").has_value());
    CHECK_FALSE(RemoteArgument(":/srv").has_value());
    CHECK_FALSE(RemoteArgument("./host:/srv").has_value());
    CHECK_FALSE(RemoteArgument("dir/host:/srv").has_value());
}

TEST_CASE("A command-line argument that exists here stays local", "[Remote][RemoteCommandLine]") {
    const fs::path dir = fs::temp_directory_path() / "ned_remote_command_line_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "host:");
    std::ofstream(dir / "host:" / "x") << "here\n";

    const fs::path previous = fs::current_path();
    fs::current_path(dir);
    const bool local = !RemoteArgument("host:/x").has_value();
    fs::current_path(previous);
    fs::remove_all(dir);
    CHECK(local);
}

TEST_CASE("A remote command-line argument is a directory, a file, or a file to create", "[Remote][RemoteCommandLine][HostAgent]") {
    ned::tests::FakeRemoteHost    fake;
    ned::tests::ScopedEnvOverride home{"HOME", (fake.dir / "home").c_str()};
    ned::editor::PostQueue        queue;
    ned::editor::remote::Hosts    hosts{queue, [&fake](const std::string& destination) { return fake.Options(destination); },
                                        ned::tests::FakeResolver(fake.dir)};
    ned::editor::LifetimeGuard    lifetime;
    std::ofstream(fake.dir / "notes.txt") << "notes\n";

    const auto classify = [&](const std::string& destination, const fs::path& path) {
        std::optional<std::expected<RemoteArgumentKind, std::string>> kind;
        ned::editor::remote::ClassifyRemoteArgument(hosts, {.destination = destination, .path = path}, lifetime.Token(),
                                                    [&](std::expected<RemoteArgumentKind, std::string> result) { kind = std::move(result); });
        REQUIRE(ned::tests::PumpUntil(queue, [&] { return kind.has_value(); }));
        return *kind;
    };
    CHECK(classify("host", fake.dir) == RemoteArgumentKind::Directory);
    CHECK(classify("host", fake.dir / "notes.txt") == RemoteArgumentKind::File);
    CHECK(classify("host", fake.dir / "new.txt") == RemoteArgumentKind::File);
    const auto unreachable = classify("denied", "/etc");
    REQUIRE_FALSE(unreachable.has_value());
    CHECK(unreachable.error().find("Permission denied") != std::string::npos);
}
