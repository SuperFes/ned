#include <catch2/catch_test_macros.hpp>

#include "Editor/SearchSettings.h"

using ned::editor::ProjectSearchThreads;
using ned::editor::SetProjectSearchThreads;

namespace {

// Mirrors TabWidthTest.cpp's own TabWidthGuard exactly -- ProjectSearchThreads
// is process-wide state (see SearchSettings.h's own doc comment).
struct ProjectSearchThreadsGuard {
    ~ProjectSearchThreadsGuard() {
        SetProjectSearchThreads(4);
    }
};

} // namespace

TEST_CASE("ProjectSearchThreads defaults to 4", "[SearchSettings]") {
    const ProjectSearchThreadsGuard guard;
    REQUIRE(ProjectSearchThreads() == 4);
}

TEST_CASE("SetProjectSearchThreads/ProjectSearchThreads round-trip", "[SearchSettings]") {
    const ProjectSearchThreadsGuard guard;
    SetProjectSearchThreads(8);
    REQUIRE(ProjectSearchThreads() == 8);
    SetProjectSearchThreads(1);
    REQUIRE(ProjectSearchThreads() == 1);
}

TEST_CASE("SetProjectSearchThreads clamps a non-positive count to 1", "[SearchSettings]") {
    const ProjectSearchThreadsGuard guard;
    SetProjectSearchThreads(0);
    REQUIRE(ProjectSearchThreads() == 1);
    SetProjectSearchThreads(-5);
    REQUIRE(ProjectSearchThreads() == 1);
}
