#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "Editor/Org.h"
#include "EnvOverride.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::org::BuildHeadlineTree;
using ned::editor::org::ClockEntry;
using ned::editor::org::ClockInAtPoint;
using ned::editor::org::ClockInStatus;
using ned::editor::org::ClockOut;
using ned::editor::org::ClockOutStatus;
using ned::editor::org::CurrentlyRunningClock;
using ned::editor::org::ElapsedMinutes;
using ned::editor::org::LocalToday;
using ned::editor::org::ParseLogbookDrawer;
using ned::editor::org::ParseOutline;
using ned::editor::org::TotalClockedMinutes;
using ned::editor::org::TotalClockedMinutesForSubtree;
using ned::text::Buffer;
using ned::text::Rope;

namespace {

// A fixed point in time so tests don't depend on when they happen to
// run -- 2026-08-24 09:15 UTC, which is local time under
// Tests/TimeZoneTestGuard.cpp.
std::chrono::system_clock::time_point TestNow(int hour, int minute) {
    using namespace std::chrono;
    const auto date = sys_days{year{2026} / August / day{24}};
    return date + hours{hour} + minutes{minute};
}

std::chrono::system_clock::time_point Utc(std::chrono::year_month_day date, int hour, int minute) {
    using namespace std::chrono;
    return sys_days{date} + hours{hour} + minutes{minute};
}

// US Pacific as a POSIX rule, so the tests don't depend on installed tzdata.
constexpr const char* kPacific = "PST8PDT,M3.2.0,M11.1.0";

} // namespace

TEST_CASE("ParseLogbookDrawer returns nullopt when there is no drawer", "[Org][Clock]") {
    const std::string text      = "* Buy milk\n";
    const auto        headlines = ParseOutline(text);
    CHECK_FALSE(ParseLogbookDrawer(text, headlines[0]).has_value());
}

TEST_CASE("ParseLogbookDrawer finds a drawer sitting right after the headline", "[Org][Clock]") {
    const std::string text      = "* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n";
    const auto        headlines = ParseOutline(text);
    const auto        drawer    = ParseLogbookDrawer(text, headlines[0]);
    REQUIRE(drawer.has_value());
    REQUIRE(drawer->entries.size() == 1);
    CHECK_FALSE(drawer->entries[0].end.has_value());
}

TEST_CASE("ParseLogbookDrawer finds a drawer sitting after an existing property drawer, not before it", "[Org][Clock]") {
    const std::string text =
        "* Buy milk\n:PROPERTIES:\n:CUSTOM_ID: milk\n:END:\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n";
    const auto headlines = ParseOutline(text);
    const auto drawer    = ParseLogbookDrawer(text, headlines[0]);
    REQUIRE(drawer.has_value());
    REQUIRE(drawer->entries.size() == 1);
}

TEST_CASE("ParseLogbookDrawer parses a closed entry with the correct duration across an hour boundary", "[Org][Clock]") {
    const std::string text =
        "* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]--[2026-08-24 Mon 11:00] =>  1:45\n:END:\n";
    const auto headlines = ParseOutline(text);
    const auto drawer    = ParseLogbookDrawer(text, headlines[0]);
    REQUIRE(drawer.has_value());
    REQUIRE(drawer->entries.size() == 1);
    const ClockEntry& entry = drawer->entries[0];
    REQUIRE(entry.end.has_value());
    REQUIRE(entry.duration.has_value());
    CHECK(entry.duration->count() == 105); // 1:45
}

TEST_CASE("ParseLogbookDrawer recomputes duration rather than trusting stale buffer text", "[Org][Clock]") {
    const std::string text =
        "* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]--[2026-08-24 Mon 11:00] =>  99:99\n:END:\n";
    const auto headlines = ParseOutline(text);
    const auto drawer    = ParseLogbookDrawer(text, headlines[0]);
    REQUIRE(drawer.has_value());
    REQUIRE(drawer->entries[0].duration.has_value());
    CHECK(drawer->entries[0].duration->count() == 105);
}

TEST_CASE("ClockInAtPoint creates a LOGBOOK drawer with a running entry from nothing", "[Org][Clock]") {
    Buffer buffer("test", Rope("* Buy milk\n"));
    buffer.SetPoint(2);
    const auto result = ClockInAtPoint(buffer, TestNow(9, 15));
    CHECK(result.status == ClockInStatus::Ok);
    CHECK(buffer.Text() == "* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n");
}

TEST_CASE("ClockInAtPoint reports NotOnHeadline off a headline", "[Org][Clock]") {
    Buffer buffer("test", Rope("* Buy milk\nBody text\n"));
    buffer.SetPoint(buffer.Text().find("Body"));
    const auto result = ClockInAtPoint(buffer, TestNow(9, 15));
    CHECK(result.status == ClockInStatus::NotOnHeadline);
    CHECK(buffer.Text() == "* Buy milk\nBody text\n"); // untouched
}

TEST_CASE("ClockInAtPoint reports AlreadyRunningHere on a second attempt on the same headline", "[Org][Clock]") {
    Buffer buffer("test", Rope("* Buy milk\n"));
    buffer.SetPoint(2);
    REQUIRE(ClockInAtPoint(buffer, TestNow(9, 15)).status == ClockInStatus::Ok);
    const auto result = ClockInAtPoint(buffer, TestNow(9, 30));
    CHECK(result.status == ClockInStatus::AlreadyRunningHere);
}

TEST_CASE("ClockInAtPoint reports AlreadyRunningElsewhere and names the other headline, without touching it", "[Org][Clock]") {
    Buffer buffer("test", Rope("* Buy milk\n* Walk dog\n"));
    buffer.SetPoint(2); // "Buy milk"
    REQUIRE(ClockInAtPoint(buffer, TestNow(9, 15)).status == ClockInStatus::Ok);
    const std::string afterFirstClockIn = buffer.Text();

    buffer.SetPoint(buffer.Text().find("Walk dog"));
    const auto result = ClockInAtPoint(buffer, TestNow(9, 30));
    CHECK(result.status == ClockInStatus::AlreadyRunningElsewhere);
    CHECK(result.otherHeadlineTitle == "Buy milk");
    CHECK(buffer.Text() == afterFirstClockIn); // "Walk dog" untouched -- no auto clock-switch
}

TEST_CASE("ClockInAtPoint appends into an existing LOGBOOK drawer", "[Org][Clock]") {
    Buffer buffer("test", Rope("* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-23 Sun 09:00]--[2026-08-23 Sun 10:00] =>  1:00\n:END:\n"));
    buffer.SetPoint(2);
    const auto result = ClockInAtPoint(buffer, TestNow(9, 15));
    CHECK(result.status == ClockInStatus::Ok);
    CHECK(buffer.Text() == "* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-23 Sun 09:00]--[2026-08-23 Sun 10:00] =>  1:00\n"
                           "CLOCK: [2026-08-24 Mon 09:15]\n:END:\n");
}

TEST_CASE("ClockOut closes the running entry in place with the correct duration", "[Org][Clock]") {
    Buffer buffer("test", Rope("* Buy milk\n"));
    buffer.SetPoint(2);
    REQUIRE(ClockInAtPoint(buffer, TestNow(9, 15)).status == ClockInStatus::Ok);
    const auto status = ClockOut(buffer, TestNow(11, 0));
    CHECK(status == ClockOutStatus::Ok);
    CHECK(buffer.Text() ==
          "* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]--[2026-08-24 Mon 11:00] =>  1:45\n:END:\n");
}

TEST_CASE("ClockOut reports NoRunningClock when nothing is running", "[Org][Clock]") {
    Buffer     buffer("test", Rope("* Buy milk\n"));
    const auto status = ClockOut(buffer, TestNow(11, 0));
    CHECK(status == ClockOutStatus::NoRunningClock);
    CHECK(buffer.Text() == "* Buy milk\n"); // untouched
}

TEST_CASE("ClockOut works regardless of where point currently is", "[Org][Clock]") {
    Buffer buffer("test", Rope("* Buy milk\n* Walk dog\n"));
    buffer.SetPoint(2);
    REQUIRE(ClockInAtPoint(buffer, TestNow(9, 15)).status == ClockInStatus::Ok);
    buffer.SetPoint(buffer.Text().find("Walk dog"));
    const auto status = ClockOut(buffer, TestNow(10, 0));
    CHECK(status == ClockOutStatus::Ok);
    CHECK(buffer.Text().find("=>  0:45") != std::string::npos);
}

TEST_CASE("TotalClockedMinutes sums closed entries and ignores a running one", "[Org][Clock]") {
    const std::string text      = "* Buy milk\n:LOGBOOK:\n"
                                  "CLOCK: [2026-08-23 Sun 09:00]--[2026-08-23 Sun 10:00] =>  1:00\n"
                                  "CLOCK: [2026-08-23 Sun 12:00]--[2026-08-23 Sun 12:30] =>  0:30\n"
                                  "CLOCK: [2026-08-24 Mon 09:00]\n"
                                  ":END:\n";
    const auto        headlines = ParseOutline(text);
    const auto        total     = TotalClockedMinutes(text, headlines[0]);
    CHECK(total.count() == 90); // 1:00 + 0:30, the still-running entry doesn't count
}

TEST_CASE("CurrentlyRunningClock finds the running headline and its start time", "[Org][Clock]") {
    const std::string text    = "* Buy milk\n* Walk dog\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n";
    const auto        running = CurrentlyRunningClock(text);
    REQUIRE(running.has_value());
    CHECK(running->headline.title == "Walk dog");
    CHECK(running->start.hour == 9);
    CHECK(running->start.minute == 15);
}

TEST_CASE("CurrentlyRunningClock returns nullopt when nothing is running", "[Org][Clock]") {
    const std::string text = "* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-23 Sun 09:00]--[2026-08-23 Sun 10:00] =>  1:00\n:END:\n";
    CHECK_FALSE(CurrentlyRunningClock(text).has_value());
}

TEST_CASE("ElapsedMinutes computes now minus start across an hour boundary", "[Org][Clock]") {
    const std::string text    = "* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n";
    const auto        running = CurrentlyRunningClock(text);
    REQUIRE(running.has_value());
    CHECK(ElapsedMinutes(running->start, TestNow(11, 0)).count() == 105); // 1:45
}

TEST_CASE("TotalClockedMinutesForSubtree rolls up a parent's own time plus every descendant's", "[Org][Clock]") {
    const std::string text      = "* Parent\n:LOGBOOK:\n"
                                  "CLOCK: [2026-08-23 Sun 09:00]--[2026-08-23 Sun 09:30] =>  0:30\n"
                                  ":END:\n"
                                  "** Child A\n:LOGBOOK:\n"
                                  "CLOCK: [2026-08-23 Sun 10:00]--[2026-08-23 Sun 11:00] =>  1:00\n"
                                  ":END:\n"
                                  "** Child B\n"
                                  "*** Grandchild\n:LOGBOOK:\n"
                                  "CLOCK: [2026-08-23 Sun 12:00]--[2026-08-23 Sun 12:15] =>  0:15\n"
                                  ":END:\n";
    const auto        headlines = ParseOutline(text);
    const auto        tree      = BuildHeadlineTree(headlines);
    REQUIRE(tree.size() == 1);
    CHECK(TotalClockedMinutesForSubtree(text, tree[0]).count() == 105); // 0:30 + 1:00 + 0:15
    REQUIRE(tree[0].children.size() == 2);
    CHECK(TotalClockedMinutesForSubtree(text, tree[0].children[0]).count() == 60); // Child A alone
    CHECK(TotalClockedMinutesForSubtree(text, tree[0].children[1]).count() == 15); // Child B has none of its own, Grandchild has 0:15
}

TEST_CASE("ClockInAtPoint puts the LOGBOOK after a property drawer that ends the buffer", "[Org][Clock]") {
    SECTION("with a trailing newline") {
        Buffer buffer("test", Rope("* Task\n:PROPERTIES:\n:ISSUE: DEV-1\n:END:\n"));
        buffer.SetPoint(2);
        REQUIRE(ClockInAtPoint(buffer, TestNow(9, 15)).status == ClockInStatus::Ok);
        CHECK(buffer.Text() == "* Task\n:PROPERTIES:\n:ISSUE: DEV-1\n:END:\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n");
    }
    SECTION("without one") {
        Buffer buffer("test", Rope("* Task\n:PROPERTIES:\n:ISSUE: DEV-1\n:END:"));
        buffer.SetPoint(2);
        REQUIRE(ClockInAtPoint(buffer, TestNow(9, 15)).status == ClockInStatus::Ok);
        CHECK(buffer.Text() == "* Task\n:PROPERTIES:\n:ISSUE: DEV-1\n:END:\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n");
    }
    const auto running = CurrentlyRunningClock("* Task\n:PROPERTIES:\n:ISSUE: DEV-1\n:END:\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n");
    REQUIRE(running);
    CHECK(ned::editor::org::ClockTimePoint(running->start) == TestNow(9, 15));
}

TEST_CASE("Clock lines are written in local time and read back to the same instant", "[Org][Clock]") {
    const ned::tests::ScopedTimeZone pacific(kPacific);
    using namespace std::chrono;
    const auto start = Utc(year{2026} / August / day{25}, 1, 0); // 18:00 PDT the day before

    Buffer buffer("test", Rope("* Task\n"));
    buffer.SetPoint(2);
    REQUIRE(ClockInAtPoint(buffer, start).status == ClockInStatus::Ok);
    CHECK(buffer.Text() == "* Task\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 18:00]\n:END:\n");

    const auto running = CurrentlyRunningClock(buffer.Text());
    REQUIRE(running);
    CHECK(ned::editor::org::ClockTimePoint(running->start) == start);
    CHECK(ElapsedMinutes(running->start, start + minutes{90}).count() == 90);
}

TEST_CASE("A clock across the end of daylight saving counts the hour that repeats", "[Org][Clock]") {
    const ned::tests::ScopedTimeZone pacific(kPacific);
    using namespace std::chrono;
    const auto start = Utc(year{2026} / November / day{1}, 7, 30);  // 00:30 PDT
    const auto end   = Utc(year{2026} / November / day{1}, 11, 30); // 03:30 PST

    Buffer buffer("test", Rope("* Task\n"));
    buffer.SetPoint(2);
    REQUIRE(ClockInAtPoint(buffer, start).status == ClockInStatus::Ok);
    REQUIRE(ClockOut(buffer, end) == ClockOutStatus::Ok);
    CHECK(buffer.Text() == "* Task\n:LOGBOOK:\nCLOCK: [2026-11-01 Sun 00:30]--[2026-11-01 Sun 03:30] =>  4:00\n:END:\n");
}

TEST_CASE("LocalToday turns over at local midnight, not UTC midnight", "[Org][Clock]") {
    using namespace std::chrono;
    const auto beforeMidnight = Utc(year{2026} / August / day{25}, 6, 59); // 23:59 PDT
    const auto afterMidnight  = Utc(year{2026} / August / day{25}, 7, 0);  // 00:00 PDT

    CHECK(LocalToday(beforeMidnight) == year{2026} / August / day{25});
    const ned::tests::ScopedTimeZone pacific(kPacific);
    CHECK(LocalToday(beforeMidnight) == year{2026} / August / day{24});
    CHECK(LocalToday(afterMidnight) == year{2026} / August / day{25});
}
