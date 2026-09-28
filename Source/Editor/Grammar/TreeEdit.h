//
// One generation transition of a parsed document: the single changed text
// region, plus every range where the tree's STRUCTURE changed between the two
// parses (parse::ChangedRanges). The second half is what bounds how far a
// local edit reached: an edit can reinterpret text well away from itself (a
// heredoc delimiter moving where its body ends, an indent change re-nesting
// the lines after it), and that shows up here as a changed range rather than
// having to be assumed of every tree an external scanner touched.
//

#ifndef NED_EDITOR_GRAMMAR_TREEEDIT_H
#define NED_EDITOR_GRAMMAR_TREEEDIT_H

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "Editor/Parse/Green.h"
#include "Text/OffsetRemap.h"
#include "Tree.h"

namespace ned::editor::grammar {

struct TreeEdit {
    text::ChangedSpan span;
    // Sorted, non-overlapping [start, end) byte ranges in the new text.
    std::vector<std::pair<std::size_t, std::size_t>> structuralChanges;
};

// `span` as the parse-level edit Tree::Edit takes, with row/column points
// read from the two texts.
[[nodiscard]] parse::InputEdit InputEditFor(std::string_view oldText, std::string_view newText,
                                            const text::ChangedSpan& span);

// `oldEdited` is the previous generation's tree with InputEditFor(span)
// already applied; `newTree` the tree parsed from the new text.
[[nodiscard]] TreeEdit DescribeTreeEdit(const Tree& oldEdited, const Tree& newTree, const text::ChangedSpan& span);

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_TREEEDIT_H
