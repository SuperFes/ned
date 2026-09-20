// Application's exit lifecycle -- the exit report queue and the SaveFailed
// latch.
//
// Small enough to be obvious, but it is the last line of defence for a
// failure the UI never got to show, and the exit code is a documented
// interface that a version control system reads to decide whether to go
// through with a commit. Both are worth holding still.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Application.h"

using Ned::Application;
using Ned::ExitStatus;
using Ned::ToExitCode;

namespace {

// Process-wide state, so a case that left anything behind would leak into
// the next one's expectations.
struct CleanExitState {
    CleanExitState() {
        Application::ResetExitStateForTesting();
    }
    ~CleanExitState() {
        Application::ResetExitStateForTesting();
    }
    CleanExitState(const CleanExitState&)            = delete;
    CleanExitState& operator=(const CleanExitState&) = delete;
};

} // namespace

TEST_CASE("Exit reports start empty and taking them clears them", "[ApplicationExit]") {
    const CleanExitState clean;

    REQUIRE(Application::TakeExitReports().empty());

    Application::ReportOnExit("something failed");
    REQUIRE(Application::TakeExitReports() == std::vector<std::string>{"something failed"});

    // Taken once, gone -- a second print must not repeat what was shown.
    REQUIRE(Application::TakeExitReports().empty());
}

TEST_CASE("Exit reports keep every message, in order, without deduplicating", "[ApplicationExit]") {
    const CleanExitState clean;

    Application::ReportOnExit("first");
    Application::ReportOnExit("second");
    Application::ReportOnExit("first"); // two identical failures are two things to know about

    REQUIRE(Application::TakeExitReports() == std::vector<std::string>{"first", "second", "first"});
}

TEST_CASE("An empty exit report is ignored", "[ApplicationExit]") {
    const CleanExitState clean;

    Application::ReportOnExit(std::string());
    REQUIRE(Application::TakeExitReports().empty());
}

TEST_CASE("A clean run exits Success", "[ApplicationExit]") {
    const CleanExitState clean;

    REQUIRE(Application::CurrentExitStatus() == ExitStatus::Success);
    REQUIRE(ToExitCode(Application::CurrentExitStatus()) == 0);
}

TEST_CASE("A failed save makes the run exit SaveFailed", "[ApplicationExit]") {
    const CleanExitState clean;

    Application::NoteSaveFailed("/tmp/doomed.txt", "doomed.txt", "ned: disk full");
    REQUIRE(Application::CurrentExitStatus() == ExitStatus::SaveFailed);
    REQUIRE(ToExitCode(Application::CurrentExitStatus()) == 3);
}

TEST_CASE("Saving successfully after a failure exits Success again", "[ApplicationExit]") {
    const CleanExitState clean;

    // What matters at exit is whether the file is on disk now, not whether
    // the road there was bumpy.
    Application::NoteSaveFailed("/tmp/retried.txt", "retried.txt", "disk full");
    Application::NoteSaveSucceeded("/tmp/retried.txt");
    REQUIRE(Application::CurrentExitStatus() == ExitStatus::Success);
}

TEST_CASE("The latch is per path, so one recovery doesn't excuse another failure", "[ApplicationExit]") {
    const CleanExitState clean;

    Application::NoteSaveFailed("/tmp/one.txt", "one.txt", "disk full");
    Application::NoteSaveFailed("/tmp/two.txt", "two.txt", "disk full");
    Application::NoteSaveSucceeded("/tmp/one.txt");

    REQUIRE(Application::CurrentExitStatus() == ExitStatus::SaveFailed);

    Application::NoteSaveSucceeded("/tmp/two.txt");
    REQUIRE(Application::CurrentExitStatus() == ExitStatus::Success);
}

TEST_CASE("Noting a success for a path that never failed is harmless", "[ApplicationExit]") {
    const CleanExitState clean;

    Application::NoteSaveSucceeded("/tmp/never-failed.txt"); // the common case, every ordinary save
    REQUIRE(Application::CurrentExitStatus() == ExitStatus::Success);
}

TEST_CASE("A failed save explains itself without repeating ned's own prefix", "[ApplicationExit]") {
    const CleanExitState clean;

    // Exception messages in this codebase already start with "ned: ", and a
    // line reading `ned: failed to save "f": ned: ...` is a tell that two
    // layers each thought they were the outermost one.
    Application::NoteSaveFailed("/tmp/f.txt", "f.txt", "ned: cannot open file for writing");

    const std::vector<std::string> reports = Application::TakeExitReports();
    REQUIRE(reports.size() == 1);
    REQUIRE(reports.front() == "ned: failed to save \"f.txt\": cannot open file for writing");
}

TEST_CASE("A failed save with no reason still names the file", "[ApplicationExit]") {
    const CleanExitState clean;

    Application::NoteSaveFailed("/tmp/f.txt", "f.txt", std::string());

    const std::vector<std::string> reports = Application::TakeExitReports();
    REQUIRE(reports.size() == 1);
    REQUIRE(reports.front() == "ned: failed to save \"f.txt\"");
}

TEST_CASE("A later success clears the exit code but keeps the explanation", "[ApplicationExit]") {
    const CleanExitState clean;

    Application::NoteSaveFailed("/tmp/f.txt", "f.txt", "disk full");
    Application::NoteSaveSucceeded("/tmp/f.txt");

    // The run exits cleanly -- the file is on disk -- but what happened on
    // the way there is still worth saying.
    REQUIRE(Application::CurrentExitStatus() == ExitStatus::Success);
    REQUIRE(Application::TakeExitReports().size() == 1);
}

TEST_CASE("The documented exit codes keep their numbers", "[ApplicationExit]") {
    // These are what a script or a version control system reads; changing
    // one is a breaking change, so the numbers are pinned here deliberately.
    REQUIRE(ToExitCode(ExitStatus::Success) == 0);
    REQUIRE(ToExitCode(ExitStatus::Failure) == 1);
    REQUIRE(ToExitCode(ExitStatus::UsageError) == 2);
    REQUIRE(ToExitCode(ExitStatus::SaveFailed) == 3);
}
