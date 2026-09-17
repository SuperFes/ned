//
// rewrite-kind rollout (kind 9, FormattingCapabilities.md's B2 "Equivalence
// rewrites" -- "quote style" is that section's own first-listed example).
// Off by default per rule kind, not merely per capture: FormatRules.h's own
// RewriteRuleValue has no built-in default, the same "nothing forced"
// stance every other kind here holds, but this kind's own doc section is
// explicit that these ship off for a second reason too -- "some are not
// semantics-preserving in every dialect" -- so a capture is only ever
// touched when its own quote-style rule is configured, same as always.
//
// The pilot construct: a `rewrite.quote` capture naming a JavaScript/
// TypeScript/TSX `string` node (never `template_string` -- a different
// grammar node entirely, naturally excluded by node type). A candidate is
// REWRITTEN only when doing so is a pure delimiter swap with no other byte
// touched -- declined outright whenever the string's own interior contains
// ANY backslash (an escape sequence this pass does not attempt to
// re-derive: an escaped target quote would need UNescaping, an escaped
// current quote would become an unnecessary-but-legal escape this pass
// chooses not to leave behind either) or an unescaped occurrence of the
// target quote character (which would need escaping to stay legal). Decline
// rather than guess, the same discipline every prior rule kind's own hazard
// (Go's ASI, PHP's three-way body, Kotlin's fieldless grammar) already
// established.
//

#ifndef NED_EDITOR_FORMATREWRITE_H
#define NED_EDITOR_FORMATREWRITE_H

#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

[[nodiscard]] std::vector<FormatTextEdit> ComputeRewriteEdits(std::string_view                  text,
                                                              std::string_view                  languageKey,
                                                              const std::vector<FormatCapture>& captures);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATREWRITE_H
