// Editor/ExitReport.h -- the queue of messages printed once the terminal is
// the shell's again.
//
// Small enough to be obvious, but it is the last line of defence for a
// failure the UI never got to show (a save that failed after its buffer was
// closed), so the "nothing is silently dropped" properties are worth
// holding still.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/ExitReport.h"

using ned::editor::ReportOnExit;
using ned::editor::TakeExitReports;

namespace {

// This is a process-wide queue, so a case that leaves anything behind would
// leak into the next one's expectations.
struct DrainedQueue {
    DrainedQueue() {
        TakeExitReports();
    }
    ~DrainedQueue() {
        TakeExitReports();
    }
    DrainedQueue(const DrainedQueue&)            = delete;
    DrainedQueue& operator=(const DrainedQueue&) = delete;
};

} // namespace

TEST_CASE("ExitReport starts empty and taking it clears it", "[ExitReport]") {
    const DrainedQueue drained;

    REQUIRE(TakeExitReports().empty());

    ReportOnExit("something failed");
    REQUIRE(TakeExitReports() == std::vector<std::string>{"something failed"});

    // Taken once, gone -- a second print must not repeat what was shown.
    REQUIRE(TakeExitReports().empty());
}

TEST_CASE("ExitReport keeps every message, in order, without deduplicating", "[ExitReport]") {
    const DrainedQueue drained;

    ReportOnExit("first");
    ReportOnExit("second");
    ReportOnExit("first"); // two identical failures are two things to know about

    REQUIRE(TakeExitReports() == std::vector<std::string>{"first", "second", "first"});
}

TEST_CASE("ExitReport ignores an empty message", "[ExitReport]") {
    const DrainedQueue drained;

    ReportOnExit(std::string());
    REQUIRE(TakeExitReports().empty());
}
