#include <catch2/catch_test_macros.hpp>

#include "Editor/PendingReExec.h"

using ned::editor::PendingReExecRequest;

TEST_CASE("TakePendingReExec returns nullopt when nothing was set", "[PendingReExec]") {
    ned::editor::ResetPendingReExecForTesting();
    REQUIRE_FALSE(ned::editor::TakePendingReExec().has_value());
}

TEST_CASE("Set then Take returns the request and clears it", "[PendingReExec]") {
    ned::editor::ResetPendingReExecForTesting();

    const PendingReExecRequest request{"/usr/bin/ned", "/home/user/project"};
    ned::editor::SetPendingReExec(request);

    const auto taken = ned::editor::TakePendingReExec();
    REQUIRE(taken.has_value());
    REQUIRE(taken->executablePath == "/usr/bin/ned");
    REQUIRE(taken->root == "/home/user/project");

    // Taking again after it was already taken returns nullopt -- one-shot.
    REQUIRE_FALSE(ned::editor::TakePendingReExec().has_value());
}

TEST_CASE("A second Set replaces the first before either is taken", "[PendingReExec]") {
    ned::editor::ResetPendingReExecForTesting();

    ned::editor::SetPendingReExec(PendingReExecRequest{"/usr/bin/ned", "/first"});
    ned::editor::SetPendingReExec(PendingReExecRequest{"/usr/bin/ned", "/second"});

    const auto taken = ned::editor::TakePendingReExec();
    REQUIRE(taken.has_value());
    REQUIRE(taken->root == "/second");
}
