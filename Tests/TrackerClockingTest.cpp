#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>

#include "Editor/Org.h"
#include "Editor/Tracker/Clocking.h"
#include "Text/Buffer.h"

namespace org     = ned::editor::org;
namespace tracker = ned::editor::tracker;

namespace {

struct ClockFileGuard {
    ~ClockFileGuard() {
        tracker::SetClockFile({});
    }
};

std::chrono::system_clock::time_point LocalTime(int year, int month, int day, int hour, int minute) {
    std::tm local{};
    local.tm_year  = year - 1900;
    local.tm_mon   = month - 1;
    local.tm_mday  = day;
    local.tm_hour  = hour;
    local.tm_min   = minute;
    local.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&local));
}

} // namespace

TEST_CASE("ClockFile defaults under XDG_DATA_HOME and can be set", "[TrackerClocking]") {
    ClockFileGuard guard;
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) {
        CHECK(tracker::ClockFile() == std::filesystem::path(data) / "ned" / "issues.org");
    }
    tracker::SetClockFile("/tmp/work.org");
    CHECK(tracker::ClockFile() == "/tmp/work.org");
    tracker::SetClockFile({});
    CHECK(tracker::ClockFile().filename() == "issues.org");
}

TEST_CASE("EnsureIssueHeading reuses an issue's heading or appends one", "[TrackerClocking]") {
    ned::text::Buffer buffer("issues.org");
    buffer.InsertAtPoint("* Notes");

    const std::size_t made = tracker::EnsureIssueHeading(buffer, {.key = "DEV-1", .title = "Fix login"}, "work");
    CHECK(buffer.Text() == "* Notes\n* DEV-1 Fix login\n:PROPERTIES:\n:ISSUE: DEV-1\n:TRACKER: work\n:END:\n");
    CHECK(made == 8);
    CHECK(tracker::EnsureIssueHeading(buffer, {.key = "DEV-1", .title = "Renamed"}, "work") == 8);
    CHECK(tracker::FindIssueHeading(buffer.Text(), "DEV-1", "work") == 8);
    // The same key on another connection is another issue.
    CHECK_FALSE(tracker::FindIssueHeading(buffer.Text(), "DEV-1", "home"));
    CHECK_FALSE(tracker::FindIssueHeading(buffer.Text(), "DEV-2", "work"));

    // A heading written by hand without :TRACKER: still counts.
    ned::text::Buffer mine("mine.org");
    mine.InsertAtPoint("* Mine\n:PROPERTIES:\n:ISSUE: #4\n:END:\n");
    CHECK(tracker::EnsureIssueHeading(mine, {.key = "#4"}, "gh") == 0);
}

TEST_CASE("RunningIssueClock turns a running clock on an issue into a worklog", "[TrackerClocking]") {
    ned::text::Buffer buffer("issues.org");
    tracker::EnsureIssueHeading(buffer, {.key = "DEV-1"}, "work");
    const auto start = LocalTime(2026, 9, 29, 10, 5);
    buffer.SetPoint(0);
    REQUIRE(org::ClockInAtPoint(buffer, start).status == org::ClockInStatus::Ok);

    const auto clock = tracker::RunningIssueClock(buffer.Text(), start + std::chrono::minutes(85));
    REQUIRE(clock);
    CHECK(clock->key == "DEV-1");
    CHECK(clock->connection == "work");
    CHECK(clock->worklog.started == std::chrono::system_clock::to_time_t(start));
    CHECK(clock->worklog.seconds == 85 * 60);

    // A clock on a heading that isn't an issue's is plain Org.
    ned::text::Buffer plain("plain.org");
    plain.InsertAtPoint("* Reading\n");
    plain.SetPoint(0);
    REQUIRE(org::ClockInAtPoint(plain, start).status == org::ClockInStatus::Ok);
    CHECK_FALSE(tracker::RunningIssueClock(plain.Text(), start + std::chrono::minutes(5)));
}

TEST_CASE("ClockDuration writes H:MM", "[TrackerClocking]") {
    CHECK(tracker::ClockDuration(0) == "0:00");
    CHECK(tracker::ClockDuration(85 * 60) == "1:25");
    CHECK(tracker::ClockDuration(10 * 3600 + 59) == "10:00");
}
