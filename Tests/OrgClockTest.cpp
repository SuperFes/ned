#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "Editor/Org.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::org::ClockEntry;
using ned::editor::org::ClockInAtPoint;
using ned::editor::org::ClockInStatus;
using ned::editor::org::ClockOut;
using ned::editor::org::ClockOutStatus;
using ned::editor::org::ParseLogbookDrawer;
using ned::editor::org::ParseOutline;
using ned::editor::org::TotalClockedMinutes;
using ned::text::Buffer;
using ned::text::Rope;

namespace {

    // A fixed point in time so tests don't depend on when they happen to
    // run -- 2026-08-24 09:15 UTC.
    std::chrono::system_clock::time_point TestNow(int hour, int minute) {
        using namespace std::chrono;
        const auto date = sys_days{year{2026} / August / day{24}};
        return date + hours{hour} + minutes{minute};
    }

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
    const std::string text = "* Buy milk\n:LOGBOOK:\n"
                             "CLOCK: [2026-08-23 Sun 09:00]--[2026-08-23 Sun 10:00] =>  1:00\n"
                             "CLOCK: [2026-08-23 Sun 12:00]--[2026-08-23 Sun 12:30] =>  0:30\n"
                             "CLOCK: [2026-08-24 Mon 09:00]\n"
                             ":END:\n";
    const auto headlines = ParseOutline(text);
    const auto total     = TotalClockedMinutes(text, headlines[0]);
    CHECK(total.count() == 90); // 1:00 + 0:30, the still-running entry doesn't count
}
