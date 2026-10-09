#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "Editor/Remote/CommandLine.h"

using ned::editor::remote::RemoteArgument;
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
