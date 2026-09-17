//
// align-kind rollout (kind 5, FormattingCapabilities.md's B2 "Alignment in
// columns"): the pilot pass for the first rule kind whose own edit decision
// needs more than one capture at a time. Every prior kind (Space/Break/
// Blank/Wrap) resolves a capture entirely against its own span; Align's
// entire point is a SHARED column across several lines, so this file is
// also where "adjacent lines forming a group" -- a notion the capabilities
// doc explicitly says nothing else in the engine has -- actually gets
// defined, deliberately AFTER kinds 2/3/4/6 shipped rather than guessed at
// ahead of time (that doc's own "worth doing after B1" stance).
//
// The grouping rule: captures sharing one NAME are split into maximal runs
// where each next capture sits on the line immediately following the
// previous one's own line (a blank line, an intervening unrelated
// statement, or simply "not adjacent" all end a run) AND shares that
// previous capture's own leading-indent text exactly (LineIndentOf,
// byte-for-byte) -- a deliberately conservative proxy for "same nesting
// depth, same immediate parent" that needs no tree at all, only the flat
// capture list every other pass here already works from. A run of size one
// has nothing to align against and contributes no edit. Pure text-column
// counting (byte offset from the line's own start), not tab-width-aware --
// a real gap for a tab-indented file with a mid-line tab before the anchor,
// logged as a follow-up rather than guessed at, the same "wrap-if-long
// needs a real margin this rollout hasn't built" honesty Wrap's own header
// comment already models.
//
// Only the whitespace strictly between the previous non-whitespace byte and
// a capture's own start byte is ever touched, the same "never touch what's
// not part of the gap" discipline FormatBracePlacement.h/FormatSpacing.h
// already hold -- so a run's own longest line is left completely untouched,
// and every other member gets exactly enough padding inserted for its own
// anchor to land in the same column. Idempotent by construction: re-running
// against an already-aligned run recomputes the identical target column and
// finds every gap already at it.
//

#ifndef NED_EDITOR_FORMATALIGN_H
#define NED_EDITOR_FORMATALIGN_H

#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

// `captures` is Mode::formatCaptures' own output, unfiltered -- a capture
// whose name has no `:enabled true` Align rule configured contributes
// nothing, and a capture name with no query-authored anchor at all is
// simply absent from the list. See this file's own header comment for the
// grouping rule.
[[nodiscard]] std::vector<FormatTextEdit> ComputeAlignEdits(std::string_view                  text,
                                                            std::string_view                  languageKey,
                                                            const std::vector<FormatCapture>& captures);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATALIGN_H
