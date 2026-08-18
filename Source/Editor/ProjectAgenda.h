//
// Project-wide TODO listing (org-agenda follow-up): a cross-file scan for
// active (non-DONE) Org TODO headlines, reusing ProjectSearch's own
// SearchMatch shape so BufferView::BuildResultsBuffer and
// project-search-visit-result (C-c C-v) need no new code at all to display
// or navigate this -- see this header's own CollectProjectTodos doc comment
// for the full story.
//
// Deliberately scoped to a TODO *list*, not a real date-driven Org agenda
// (today's scheduled items, overdue deadlines): this codebase has
// SCHEDULED:/DEADLINE: syntax highlighting but no structured timestamp
// parsing at all yet, a separate, larger, not-yet-started piece of work
// (see ROADMAP.md). No date logic here.
//

#ifndef NED_EDITOR_PROJECTAGENDA_H
#define NED_EDITOR_PROJECTAGENDA_H

#include <filesystem>
#include <string>
#include <vector>

#include "Org.h"
#include "ProjectSearch.h"

namespace ned::editor {

// Scans every ".org" file under root (via ProjectTree::BuildProjectTree --
// the same directories-before-files, alphabetical, dot-dir-skipping walk
// ProjectSearch/ProjectSidebar already use) for "active" TODO headlines --
// org::ParseOutline(fileText, todoKeywords) per file, keeping a headline
// when !todoKeyword.empty() && todoKeyword != todoKeywords.back(), the
// exact "last configured keyword is the done state" convention Mode.cpp's
// own CaptureTable-driven TodoKeyword/DoneKeyword highlighting split
// already established, not a new rule invented here.
//
// Returns SearchMatch{file, lineNumber, lineText} directly (ProjectSearch.h)
// -- the exact type BufferView::BuildResultsBuffer already renders and
// project-search-visit-result already jumps to, so this needs no new UI
// code to display or navigate. lineText is a reconstructed one-line
// summary: "<keyword>[ [#priority]] <title>[ :tag1:tag2:]", e.g.
// "TODO [#A] Buy milk :errand:". Files are visited in BuildProjectTree's
// own order; within a file, headlines are in document order -- no
// priority-based re-sorting, a deliberate v1 simplicity choice. Returns an
// empty list rather than throwing for a nonexistent/unlistable root,
// matching SearchDirectory's own convention.
[[nodiscard]] std::vector<SearchMatch> CollectProjectTodos(const std::filesystem::path&    root,
                                                            const std::vector<std::string>& todoKeywords = org::TodoKeywords());

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTAGENDA_H
