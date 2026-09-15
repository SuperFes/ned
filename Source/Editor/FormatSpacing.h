//
// configurable-formatter-rules follow-up: the Space-kind (kind 2) pass,
// FormatBracePlacement.h's sibling -- same pure-compute/thin-apply split,
// same "a capture with no configured rule contributes nothing" contract.
// Proven end to end against cpp's "control.parens" pilot capture
// (Source/Languages/cpp/format.janet): an if/while statement's own
// condition_clause, whose span is exactly its "(...)" -- first byte the
// opening paren, last byte the closing one.
//

#ifndef NED_EDITOR_FORMATSPACING_H
#define NED_EDITOR_FORMATSPACING_H

#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

// Computes the edits needed to make every capture in `captures` whose name
// resolves any field of SpaceRuleFor(name, languageKey) match it --
// `before`/`after` govern the horizontal whitespace immediately outside the
// capture's own span, `within` the horizontal whitespace immediately inside
// its first and last byte (so a capture used for :within is expected to
// itself be the delimited span -- opening bracket as its own first byte,
// closing as its own last, the same contract cpp's "control.parens" already
// satisfies by capturing condition_clause whole).
//
// Deliberately never crosses a newline: a whitespace run this function would
// adjust is scanned outward only through ' '/'\t', stopping (and skipping
// that gap entirely, emitting nothing for it) the instant it would cross a
// '\n'/'\r' -- FormattingCapabilities.md's "Keep existing line breaks"
// stance means a Space rule never second-guesses wherever a Break-kind rule
// (or simply the file's own existing layout) already put a line boundary.
// Same idempotence guarantee as ComputeBracePlacementEdits: recomputing
// against this function's own output finds every desired gap already
// present and emits nothing.
[[nodiscard]] std::vector<FormatTextEdit> ComputeSpaceEdits(std::string_view                 text,
                                                             std::string_view                 languageKey,
                                                             const std::vector<FormatCapture>& captures);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATSPACING_H
