//
// configurable-formatter-rules follow-up: the pilot Break-kind (kind 3,
// brace placement) pass -- the first thing to actually READ Editor/
// FormatRules.h's Space/Break storage and Mode::formatCaptures. Proves the
// full chain end to end (format.janet -> Mode::formatCaptures ->
// BreakRuleFor -> a computed edit -> applied to a live Buffer) for exactly
// one construct (cpp's "brace.function" pilot capture,
// Source/Languages/cpp/format.janet) before any other rule kind or
// language gets one.
//
// Pure/buffer-free compute, thin Buffer-mutating apply -- Org.h's own split
// (ParseOutline vs. SetHeadlineTodoKeyword), same reasoning: the decision of
// WHAT to change is unit-testable with no Buffer/Parser/Screen at all, and
// the apply step (Editor/FormatEdit.h's ApplyFormatTextEdits, shared with
// Editor/FormatSpacing.h's Space-kind pass) is a couple of lines once the
// edit list exists.
//

#ifndef NED_EDITOR_FORMATBRACEPLACEMENT_H
#define NED_EDITOR_FORMATBRACEPLACEMENT_H

#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

// Computes the edits needed to make every capture in `captures` whose name
// resolves a BreakRuleFor(name, languageKey).placement match it -- nothing
// else in BreakRuleValue is read yet (collapse-empty/collapse-simple are a
// follow-up once this pilot proves out). A capture with no configured
// placement contributes no edit at all: FormattingCapabilities.md's stance
// is that nothing is forced by default, and this file ships no built-in
// placement default the way IndentDefaults.cpp does for indent width.
//
// `captures` is Mode::formatCaptures' own output, unfiltered -- a capture
// this function doesn't recognize (no placement configured for its name) is
// silently skipped, so composing this with Editor/FormatSpacing.h's
// Space-kind consumer over the same capture list needs no coordination
// between the two.
//
// Only the whitespace strictly between a capture's own header (its last
// non-whitespace byte) and the capture's own start byte is ever touched --
// nothing inside the brace-carrying construct's body, and nothing before
// its header. Idempotent by construction: re-running against the result of
// a previous run recomputes the same desired gap and finds it already
// present, emitting nothing (Tests/FormatterPropertiesTest.cpp's own
// idempotence property this codebase holds every formatting change to).
[[nodiscard]] std::vector<FormatTextEdit> ComputeBracePlacementEdits(std::string_view                 text,
                                                                     std::string_view                 languageKey,
                                                                     const std::vector<FormatCapture>& captures);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATBRACEPLACEMENT_H
