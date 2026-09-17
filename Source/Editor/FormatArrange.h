//
// arrange-kind rollout (kind 8, FormattingCapabilities.md's B2 "Import
// organisation" half -- the "Tree-sitter handles all of it" one; "Member
// arrangement" stays unstarted, see FormatRules.h's own ArrangeRuleValue
// comment for why). The pilot construct: a whole import/include STATEMENT
// captured as one node (`arrange.import` -- cpp's own `preproc_include`,
// JavaScript/TypeScript's own `import_statement`), reordered among its own
// immediate run of adjacent siblings by the captured text itself.
//
// Grouping mirrors Align's own rule (Editor/FormatAlign.h): a maximal run
// of same-name captures where each next one starts on the line immediately
// following the previous one's own LAST line -- a blank line (or anything
// else) between two imports is a deliberate group boundary, the same
// "group plain vs. from imports separately" instinct JetBrains' own pane
// already has, gotten essentially for free from line-adjacency alone
// rather than a real from/plain classification this pilot doesn't attempt.
// A capture whose own span crosses a newline (a multi-line import) is
// dropped before grouping even begins -- reordering it would mean moving
// something other than "one line", which this pass's own per-line-text
// reorder mechanism cannot safely do; decline rather than approximate, the
// same call FormatWrap.h's own item-shape checks make.
//
// The sort key is the captured node's own text verbatim -- not a resolved
// module path, not the specifier alone. A real per-language "sort by module
// path, not by whatever aliasing syntax precedes it" refinement is a
// follow-up, not attempted here (this pilot's own narrow scope, matching
// every prior rule kind's own first capture).
//

#ifndef NED_EDITOR_FORMATARRANGE_H
#define NED_EDITOR_FORMATARRANGE_H

#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

[[nodiscard]] std::vector<FormatTextEdit> ComputeArrangeEdits(std::string_view                  text,
                                                              std::string_view                  languageKey,
                                                              const std::vector<FormatCapture>& captures);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATARRANGE_H
