#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "Editor/ProjectAgenda.h"

using ned::editor::AgendaItem;
using ned::editor::AgendaSection;
using ned::editor::CollectAgendaItems;

namespace {

bool AnyItemWithSummary(const std::vector<AgendaItem>& items, const std::string& summary) {
    return std::any_of(items.begin(), items.end(), [&](const AgendaItem& item) { return ned::editor::FormatAgendaItemSummary(item) == summary; });
}

const AgendaItem* FindByTitle(const std::vector<AgendaItem>& items, const std::string& title) {
    const auto it = std::find_if(items.begin(), items.end(), [&](const AgendaItem& item) { return item.headline.title == title; });
    return it == items.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("CollectAgendaItems finds active TODOs across multiple .org files, excluding DONE and plain headlines",
          "[ProjectAgenda]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_agenda_test_basic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    {
        std::ofstream(dir / "a.org") << "* TODO Buy milk\n* DONE Buy eggs\n* Just a headline\n";
    }
    {
        std::ofstream(dir / "b.org") << "* TODO [#A] Fix the roof :urgent:home:\n";
    }
    // A non-.org file with the same shape must be ignored entirely.
    {
        std::ofstream(dir / "c.txt") << "* TODO not an org file\n";
    }

    const std::vector<AgendaItem> items = CollectAgendaItems(dir);

    REQUIRE(items.size() == 2);
    REQUIRE(AnyItemWithSummary(items, "TODO Buy milk"));
    REQUIRE(AnyItemWithSummary(items, "TODO [#A] Fix the roof :urgent:home:"));

    const AgendaItem* milk = FindByTitle(items, "Buy milk");
    REQUIRE(milk != nullptr);
    REQUIRE(milk->file.filename() == "a.org");
    REQUIRE(milk->section == AgendaSection::Undated); // no SCHEDULED:/DEADLINE: at all

    std::filesystem::remove_all(dir);
}

TEST_CASE("CollectAgendaItems respects a custom todoKeywords list's own last-is-done convention", "[ProjectAgenda]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_agenda_test_custom_keywords";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    {
        std::ofstream(dir / "a.org") << "* TODO Step one\n* IN-PROGRESS Step two\n* COMPLETE Step three\n";
    }

    const std::vector<AgendaItem> items = CollectAgendaItems(dir, {"TODO", "IN-PROGRESS", "COMPLETE"});

    REQUIRE(items.size() == 2); // COMPLETE (the last/configured-done keyword) is excluded
    REQUIRE(AnyItemWithSummary(items, "TODO Step one"));
    REQUIRE(AnyItemWithSummary(items, "IN-PROGRESS Step two"));
    REQUIRE_FALSE(AnyItemWithSummary(items, "COMPLETE Step three"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("CollectAgendaItems returns an empty list for a nonexistent root, not a throw", "[ProjectAgenda]") {
    const std::vector<AgendaItem> items =
        CollectAgendaItems(std::filesystem::temp_directory_path() / "ned_project_agenda_test_does_not_exist");
    REQUIRE(items.empty());
}

TEST_CASE("CollectAgendaItems buckets by SCHEDULED:/DEADLINE: against a fixed reference date", "[ProjectAgenda]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_agenda_test_buckets";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    {
        std::ofstream(dir / "a.org") << "* TODO Overdue task\nDEADLINE: <2026-08-20 Thu>\n"
                                     << "* TODO Due today (scheduled)\nSCHEDULED: <2026-08-23 Sun>\n"
                                     << "* TODO Due today (deadline)\nDEADLINE: <2026-08-23 Sun>\n"
                                     << "* TODO Upcoming task\nSCHEDULED: <2026-08-30 Sun>\n"
                                     << "* TODO Undated task\n";
    }

    const std::chrono::year_month_day referenceDate = std::chrono::year{2026} / std::chrono::August / std::chrono::day{23};
    const std::vector<AgendaItem>     items         = CollectAgendaItems(dir, ned::editor::org::TodoKeywords(), referenceDate);

    REQUIRE(items.size() == 5);

    const AgendaItem* overdue = FindByTitle(items, "Overdue task");
    REQUIRE(overdue != nullptr);
    REQUIRE(overdue->section == AgendaSection::Overdue);
    REQUIRE(overdue->dateKind == AgendaItem::DateKind::Deadline);

    const AgendaItem* todayScheduled = FindByTitle(items, "Due today (scheduled)");
    REQUIRE(todayScheduled != nullptr);
    REQUIRE(todayScheduled->section == AgendaSection::Today);

    const AgendaItem* todayDeadline = FindByTitle(items, "Due today (deadline)");
    REQUIRE(todayDeadline != nullptr);
    REQUIRE(todayDeadline->section == AgendaSection::Today);

    const AgendaItem* upcoming = FindByTitle(items, "Upcoming task");
    REQUIRE(upcoming != nullptr);
    REQUIRE(upcoming->section == AgendaSection::Upcoming);

    const AgendaItem* undated = FindByTitle(items, "Undated task");
    REQUIRE(undated != nullptr);
    REQUIRE(undated->section == AgendaSection::Undated);

    // Sorted by section in AgendaSection's own declared order: Overdue,
    // Today, Upcoming, Undated.
    REQUIRE(items.front().section == AgendaSection::Overdue);
    REQUIRE(items.back().section == AgendaSection::Undated);

    std::filesystem::remove_all(dir);
}

TEST_CASE("CollectAgendaItems prefers a DEADLINE: over a SCHEDULED: when a headline has both", "[ProjectAgenda]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_agenda_test_deadline_priority";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    {
        std::ofstream(dir / "a.org") << "* TODO Both dates\nSCHEDULED: <2026-08-10 Mon> DEADLINE: <2026-08-25 Tue>\n";
    }

    const std::chrono::year_month_day referenceDate = std::chrono::year{2026} / std::chrono::August / std::chrono::day{23};
    const std::vector<AgendaItem>     items         = CollectAgendaItems(dir, ned::editor::org::TodoKeywords(), referenceDate);

    REQUIRE(items.size() == 1);
    REQUIRE(items.front().dateKind == AgendaItem::DateKind::Deadline);
    REQUIRE(items.front().section == AgendaSection::Upcoming); // 2026-08-25 > referenceDate

    std::filesystem::remove_all(dir);
}
