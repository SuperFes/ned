#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "Text/DiskSpace.h"

using ned::text::CheckFreeSpaceForSave;
using ned::text::DiskSpaceCheck;

TEST_CASE("CheckFreeSpaceForSave with an injected override: sufficient", "[DiskSpace]") {
    const DiskSpaceCheck check = CheckFreeSpaceForSave("/tmp/does-not-matter.txt", /*contentBytes=*/100, /*multiplier=*/2.0,
                                                        /*availableBytesOverride=*/250);
    REQUIRE(check.sufficient);
    REQUIRE(check.requiredBytes == 200);
    REQUIRE(check.availableBytes == 250);
}

TEST_CASE("CheckFreeSpaceForSave with an injected override: insufficient", "[DiskSpace]") {
    const DiskSpaceCheck check = CheckFreeSpaceForSave("/tmp/does-not-matter.txt", /*contentBytes=*/100, /*multiplier=*/2.0,
                                                        /*availableBytesOverride=*/199);
    REQUIRE_FALSE(check.sufficient);
    REQUIRE(check.requiredBytes == 200);
    REQUIRE(check.availableBytes == 199);
}

TEST_CASE("CheckFreeSpaceForSave at exactly the boundary is sufficient", "[DiskSpace]") {
    const DiskSpaceCheck check = CheckFreeSpaceForSave("/tmp/does-not-matter.txt", 100, 2.0, /*availableBytesOverride=*/200);
    REQUIRE(check.sufficient); // >=, not >
}

TEST_CASE("CheckFreeSpaceForSave against the real filesystem for an existing path", "[DiskSpace]") {
    // No override -- exercises the real std::filesystem::space() path. The
    // temp directory is presumably not full, so this should pass with a
    // trivially small content size; the real value isn't asserted (that
    // would make the test depend on the machine's actual free space), only
    // that the real path is reachable and returns a plausible answer.
    const DiskSpaceCheck check = CheckFreeSpaceForSave(std::filesystem::temp_directory_path(), /*contentBytes=*/1, /*multiplier=*/1.0);
    REQUIRE(check.sufficient);
    REQUIRE(check.availableBytes > 0);
}

TEST_CASE("CheckFreeSpaceForSave fails safe for an unstatable path", "[DiskSpace]") {
    const DiskSpaceCheck check =
        CheckFreeSpaceForSave("/this/path/almost-certainly/does-not-exist/at-all", /*contentBytes=*/1, /*multiplier=*/1.0);
    REQUIRE_FALSE(check.sufficient);
}
