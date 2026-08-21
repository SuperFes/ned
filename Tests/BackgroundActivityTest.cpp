#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/BackgroundActivity.h"

using ned::editor::ActiveBackgroundActivities;
using ned::editor::BackgroundActivity;
using ned::editor::BeginBackgroundActivity;
using ned::editor::EndBackgroundActivity;
using ned::editor::SetBackgroundActivityDetail;

// The registry is process-wide state (the TabWidth.h/ProjectRoot.h pattern),
// so every test here must leave it empty -- the same clean-up-after-yourself
// convention LspManagerTest's SetLspServerCommand tests follow.

TEST_CASE("BackgroundActivity begin/end pairs make a name active exactly while counted", "[BackgroundActivity]") {
    REQUIRE(ActiveBackgroundActivities().empty());

    BeginBackgroundActivity("test-activity");
    REQUIRE(ActiveBackgroundActivities() == std::vector<BackgroundActivity>{{.name = "test-activity", .detail = ""}});

    BeginBackgroundActivity("test-activity"); // second begin -- still one entry, counted twice
    REQUIRE(ActiveBackgroundActivities().size() == 1);

    EndBackgroundActivity("test-activity");
    REQUIRE(ActiveBackgroundActivities().size() == 1); // one begin still outstanding

    EndBackgroundActivity("test-activity");
    REQUIRE(ActiveBackgroundActivities().empty());
}

TEST_CASE("BackgroundActivity end without a begin clamps instead of going negative", "[BackgroundActivity]") {
    EndBackgroundActivity("never-begun");
    REQUIRE(ActiveBackgroundActivities().empty());

    // The clamp must not have banked a negative count -- one begin is active again immediately.
    BeginBackgroundActivity("never-begun");
    REQUIRE(ActiveBackgroundActivities().size() == 1);
    EndBackgroundActivity("never-begun");
    REQUIRE(ActiveBackgroundActivities().empty());
}

TEST_CASE("BackgroundActivity detail attaches to an active entry and dies with it", "[BackgroundActivity]") {
    SetBackgroundActivityDetail("inactive", "ignored"); // no active entry -- must not create one
    REQUIRE(ActiveBackgroundActivities().empty());

    BeginBackgroundActivity("worker");
    SetBackgroundActivityDetail("worker", "indexing (45%)");
    REQUIRE(ActiveBackgroundActivities() == std::vector<BackgroundActivity>{{.name = "worker", .detail = "indexing (45%)"}});

    EndBackgroundActivity("worker");
    REQUIRE(ActiveBackgroundActivities().empty());

    BeginBackgroundActivity("worker"); // fresh entry -- the old detail must not resurrect
    REQUIRE(ActiveBackgroundActivities() == std::vector<BackgroundActivity>{{.name = "worker", .detail = ""}});
    EndBackgroundActivity("worker");
}

TEST_CASE("ActiveBackgroundActivities returns entries sorted by name", "[BackgroundActivity]") {
    BeginBackgroundActivity("zeta");
    BeginBackgroundActivity("alpha");

    const std::vector<BackgroundActivity> active = ActiveBackgroundActivities();
    REQUIRE(active.size() == 2);
    REQUIRE(active[0].name == "alpha");
    REQUIRE(active[1].name == "zeta");

    EndBackgroundActivity("zeta");
    EndBackgroundActivity("alpha");
    REQUIRE(ActiveBackgroundActivities().empty());
}
