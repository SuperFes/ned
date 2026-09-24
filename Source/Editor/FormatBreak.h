//
// keyword-break follow-up: the half of the Break kind (kind 3) that is not
// brace placement -- `:before`/`:after`, "a mandatory or forbidden newline at
// this point". FormatBracePlacement.h decides where a body's own delimiters
// sit; this decides where the KEYWORD introducing a continuation clause sits
// relative to the clause that came before it.
//
// The construct this exists for is Allman's own `else`:
//
//     }              }
//     else     vs    } else
//     {              {
//
// Brace placement alone cannot express it. A body capture's span starts at
// its opening brace, so no rule about it can say anything about a keyword
// standing outside and before it -- which is why `:before`/`:after` were
// stored and resolved from the start (FormatRules.h, format.janet's schema,
// ned/set-format-break-before) but read by nothing until this pass existed.
//
// The capture names the KEYWORD TOKEN itself, not a clause or a body -- a
// new convention this kind needed for the same reason every prior kind
// needed its own: a rule can only talk about a span some query actually
// names. One shared name, `control.keyword`, covers every continuation keyword
// a language has (`else`, `elseif`, `catch`, `finally`, do-while's trailing
// `while`), the same grouping `brace.control` already makes for those same
// statements' bodies -- "break before a control keyword" is one decision in
// every style guide that has an opinion about it.
//
// Unconfigured is a total no-op, the rule every other kind here follows.
//

#ifndef NED_EDITOR_FORMATBREAK_H
#define NED_EDITOR_FORMATBREAK_H

#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

// Computes the edits needed to make every capture whose name resolves
// `before` or `after` in BreakRuleFor(name, languageKey) match it.
//
// `before: true` puts the capture on a line of its own, indented to match the
// line the preceding non-whitespace byte sits on -- so `} else` becomes `}`
// then `else` at the closer's own column, which is what "Allman" means for a
// continuation keyword. `before: false` joins it back up with exactly one
// space. `after` is the same pair on the far side of the token.
//
// Three declines, all deliberate and all matching this engine's existing
// habit of leaving alone what it cannot reason about:
//
//   - `false` joins a gap that spans lines only when another capture (the
//     preceding body, e.g. brace.control) ends exactly where the gap starts,
//     so `}\nelse` becomes `} else` but `} // done\nelse` is left alone --
//     joining that would comment the keyword out, and the byte before its
//     gap is the comment's, not a captured closer's. Anything else only has
//     its horizontal whitespace normalised (`}    else` -> `} else`).
//   - `true` rewrites only the whitespace run immediately touching the token,
//     so a comment between the two (`} /* done */ else`) stays exactly where
//     it is and the keyword breaks after it. Inserting a newline can never
//     swallow anything, which is why this direction needs no comment check.
//   - `true` declines when nothing shares the preceding token's line -- there
//     is no closer's column to inherit.
//
// Idempotent: recomputing against this function's own output emits nothing.
[[nodiscard]] std::vector<FormatTextEdit> ComputeBreakEdits(std::string_view                  text,
                                                            std::string_view                  languageKey,
                                                            const std::vector<FormatCapture>& captures);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATBREAK_H
