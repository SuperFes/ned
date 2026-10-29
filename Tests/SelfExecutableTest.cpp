#include "Editor/Process/SelfExecutable.h"

#include <filesystem>
#include <optional>

#include <catch2/catch_test_macros.hpp>

using ned::editor::process::ResolveSelfExecutable;
using ned::editor::process::SelfExecutable;
using Path = std::filesystem::path;

TEST_CASE("The self executable is /proc/self/exe outside an AppImage", "[SelfExecutable]") {
    CHECK(ResolveSelfExecutable({.procSelfExe = Path("/usr/bin/ned")}) == Path("/usr/bin/ned"));
    CHECK_FALSE(ResolveSelfExecutable({}).has_value());
    CHECK(SelfExecutable() == std::filesystem::read_symlink("/proc/self/exe"));
}

TEST_CASE("Inside an AppImage the image itself is run", "[SelfExecutable]") {
    const Path image("/home/user/Apps/ned-x86_64.AppImage");
    CHECK(ResolveSelfExecutable({.procSelfExe = Path("/tmp/.mount_nedAbC/usr/bin/ned"), .appImage = image, .appDir = Path("/tmp/.mount_nedAbC")}) ==
          image);
    CHECK(ResolveSelfExecutable({.procSelfExe = Path("/tmp/.mount_nedAbC/usr/bin/ned"), .appImage = image, .appDir = Path("/tmp/.mount_nedAbC/")}) ==
          image);
}

TEST_CASE("An AppImage's variables inherited by another ned are ignored", "[SelfExecutable]") {
    const Path image("/home/user/Apps/ned-x86_64.AppImage");
    CHECK(ResolveSelfExecutable({.procSelfExe = Path("/usr/bin/ned"), .appImage = image, .appDir = Path("/tmp/.mount_nedAbC")}) ==
          Path("/usr/bin/ned"));
    CHECK(ResolveSelfExecutable({.procSelfExe = Path("/tmp/.mount_nedAbCd/usr/bin/ned"), .appImage = image, .appDir = Path("/tmp/.mount_nedAbC")}) ==
          Path("/tmp/.mount_nedAbCd/usr/bin/ned"));
    CHECK(ResolveSelfExecutable({.procSelfExe = Path("/tmp/.mount_nedAbC/usr/bin/ned"), .appDir = Path("/tmp/.mount_nedAbC")}) ==
          Path("/tmp/.mount_nedAbC/usr/bin/ned"));
}
