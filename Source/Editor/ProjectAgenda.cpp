#include "ProjectAgenda.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "ProjectTree.h"

namespace ned::editor {

namespace {

    std::string ReadFileOrEmpty(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

} // namespace

std::string FormatAgendaItemSummary(const AgendaItem& item) {
    std::string summary = item.headline.todoKeyword;
    if (item.headline.priority) {
        summary += " [#";
        summary += *item.headline.priority;
        summary += ']';
    }
    summary += ' ';
    summary += item.headline.title;
    if (!item.headline.tags.empty()) {
        summary += " :";
        for (const std::string& tag : item.headline.tags) {
            summary += tag;
            summary += ':';
        }
    }
    if (item.relevantDate) {
        summary += (item.dateKind == AgendaItem::DateKind::Deadline) ? "  DEADLINE: " : "  SCHEDULED: ";
        summary += org::FormatTimestamp(*item.relevantDate);
    }
    return summary;
}

std::vector<AgendaItem> CollectAgendaItems(const std::filesystem::path& root, const std::vector<std::string>& todoKeywords,
                                           std::optional<std::chrono::year_month_day> referenceDate) {
    std::vector<AgendaItem> items;
    if (todoKeywords.empty()) {
        return items; // nothing can ever be "the done state" -- nothing to collect
    }
    const std::string&                doneKeyword = todoKeywords.back();
    const std::chrono::year_month_day today =
        referenceDate.value_or(std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())});

    for (const ProjectTreeEntry& entry : BuildProjectTree(root)) {
        if (entry.isDirectory || entry.path.extension() != ".org") {
            continue;
        }

        const std::string fileText = ReadFileOrEmpty(entry.path);
        for (const org::Headline& headline : org::ParseOutline(fileText, todoKeywords)) {
            if (headline.todoKeyword.empty() || headline.todoKeyword == doneKeyword) {
                continue; // no keyword at all, or the configured "done" state
            }

            const auto planning = org::ParsePlanning(fileText, headline);

            AgendaItem item;
            item.file     = entry.path;
            item.headline = headline;

            if (planning && planning->deadline) {
                item.dateKind     = AgendaItem::DateKind::Deadline;
                item.relevantDate = planning->deadline;
            }
            else if (planning && planning->scheduled) {
                item.dateKind     = AgendaItem::DateKind::Scheduled;
                item.relevantDate = planning->scheduled;
            }

            if (!item.relevantDate) {
                item.section = AgendaSection::Undated;
            }
            else if (std::chrono::sys_days{item.relevantDate->date} < std::chrono::sys_days{today}) {
                item.section = AgendaSection::Overdue;
            }
            else if (item.relevantDate->date == today) {
                item.section = AgendaSection::Today;
            }
            else {
                item.section = AgendaSection::Upcoming;
            }

            items.push_back(std::move(item));
        }
    }

    std::stable_sort(items.begin(), items.end(), [](const AgendaItem& a, const AgendaItem& b) {
        if (a.section != b.section) {
            return a.section < b.section;
        }
        if ((a.section == AgendaSection::Overdue || a.section == AgendaSection::Upcoming) && a.relevantDate && b.relevantDate) {
            return std::chrono::sys_days{a.relevantDate->date} < std::chrono::sys_days{b.relevantDate->date};
        }
        return false; // keep each file's own walk/document order as the tiebreak
    });

    return items;
}

} // namespace ned::editor
