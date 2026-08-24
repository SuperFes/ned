//
// Date-driven Org agenda (org-agenda follow-up, scheduling/recurrence
// follow-up): a cross-file scan for active (non-DONE) Org TODO headlines,
// bucketed by SCHEDULED:/DEADLINE: urgency against a reference date --
// see this header's own CollectAgendaItems doc comment for the full story.
// Source/UI/BufferView.cpp's BuildAgendaMultibuffer is the sole consumer,
// rendering this as a sectioned Editor/Multibuffer.h view (one excerpt per
// item, grouped Overdue/Today/Upcoming/Undated) rather than the flat
// SearchMatch-based results list this used to build before real timestamp
// parsing existed.
//

#ifndef NED_EDITOR_PROJECTAGENDA_H
#define NED_EDITOR_PROJECTAGENDA_H

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Org.h"

namespace ned::editor {

// Which agenda bucket an active headline falls into, relative to a
// reference date. Declaration order IS sort order (CollectAgendaItems'
// own stable_sort compares this directly) -- Overdue/Today/Upcoming ahead
// of Undated, matching what an agenda reader wants to see first. Undated
// (no SCHEDULED:/DEADLINE: at all) keeps this a strict superset of the old
// flat "every open TODO" list -- nothing regresses for a file with no
// timestamps in it at all.
enum class AgendaSection { Overdue,
                           Today,
                           Upcoming,
                           Undated };

struct AgendaItem {
    std::filesystem::path file;
    org::Headline         headline;
    AgendaSection         section;

    // Which of the headline's own planning entries `relevantDate` was
    // picked from -- a DEADLINE: is preferred over a SCHEDULED: when a
    // headline has both (real Org's own "deadline is the more urgent of
    // the two" convention), so at most one date drives bucketing/sorting
    // per headline, not one agenda entry per timestamp the way real Org's
    // own agenda view works. None/nullopt together mean Undated.
    enum class DateKind { None,
                          Scheduled,
                          Deadline };
    DateKind                         dateKind = DateKind::None;
    std::optional<org::OrgTimestamp> relevantDate;
};

// One human-readable summary line for item -- "<keyword>[ [#priority]]
// <title>[ :tag1:tag2:][  SCHEDULED: <timestamp>]", e.g. "TODO [#A] Buy
// milk :errand:  DEADLINE: <2026-08-25 Tue>". Shared by
// BuildAgendaMultibuffer so it needs no formatting logic of its own beyond
// the header line naming the file/section.
[[nodiscard]] std::string FormatAgendaItemSummary(const AgendaItem& item);

// Scans every ".org" file under root (via ProjectTree::BuildProjectTree --
// the same directories-before-files, alphabetical, dot-dir-skipping walk
// ProjectSearch/ProjectSidebar already use) for "active" TODO headlines --
// org::ParseOutline(fileText, todoKeywords) per file, keeping a headline
// when !todoKeyword.empty() && todoKeyword != todoKeywords.back(), the
// exact "last configured keyword is the done state" convention Mode.cpp's
// own CaptureTable-driven TodoKeyword/DoneKeyword highlighting split
// already established.
//
// Each kept headline's own org::ParsePlanning result picks a single
// "relevant" timestamp (deadline preferred over scheduled -- see
// AgendaItem::dateKind's own doc comment) and buckets it against
// referenceDate: strictly before -> Overdue, equal -> Today, strictly
// after -> Upcoming; no planning at all -> Undated. Sorted by section in
// AgendaSection's own declared order, then by date ascending within
// Overdue/Upcoming (oldest-overdue and soonest-upcoming first,
// respectively), preserving each file's own walk/document order as the
// tiebreak otherwise (a stable sort).
//
// referenceDate defaults to nullopt, meaning "today" read fresh from
// std::chrono::system_clock -- the same optional-override-for-testing
// shape Editor/Backup.h's PruneBackups(nowSeconds) already establishes, so
// tests can pin a fixed date without this function ever reading the wall
// clock itself when one is given. Returns an empty list rather than
// throwing for a nonexistent/unlistable root, matching SearchDirectory's
// own convention.
[[nodiscard]] std::vector<AgendaItem>
CollectAgendaItems(const std::filesystem::path& root, const std::vector<std::string>& todoKeywords = org::TodoKeywords(),
                   std::optional<std::chrono::year_month_day> referenceDate = std::nullopt);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTAGENDA_H
