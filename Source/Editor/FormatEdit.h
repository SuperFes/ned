//
// configurable-formatter-rules follow-up: the generic edit shape every
// format-rule pass (brace placement, spacing, eventually wrap/blank/...)
// computes into -- lifted out of FormatBracePlacement.h once a second
// consumer (FormatSpacing.h) needed the exact same shape, rather than
// guessed at ahead of time.
//

#ifndef NED_EDITOR_FORMATEDIT_H
#define NED_EDITOR_FORMATEDIT_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor {

// [start, end) of the buffer's current text, replaced by `text`.
struct FormatTextEdit {
    std::size_t start;
    std::size_t end;
    std::string text;
};

// Applies `edits` to `buffer` as one undo step. `edits` need not be sorted --
// this sorts (by start) and applies back-to-front so an earlier edit's
// offsets stay valid while a later one is applied. A no-op (empty `edits`)
// touches the buffer/undo tree not at all, matching ApplyHygienePass's own
// convention (Editor/Format.h).
void ApplyFormatTextEdits(text::Buffer& buffer, std::vector<FormatTextEdit> edits);

// keyword-delimiter-captures follow-up: whether gluing two bytes directly
// together would fuse them into one word -- true for any ASCII letter/
// digit/underscore. A brace/paren is never a word byte, so this is always
// false for a single-character delimiter (every capture before Lua's own).
// Shared between FormatBracePlacement.h's collapse-empty glue (Lua's
// "do"+"end" must not become "doend") and FormatSpacing.h's :within=false
// removal (fish's "begin"+"echo" must not become "beginecho") -- the same
// underlying hazard, reached from two different rule kinds.
bool IsWordByte(char c);

// wrap-kind follow-up: lifted out of FormatBracePlacement.h once a second
// consumer (FormatWrap.h) needed the exact same two helpers, the same
// "extract once a real second use shows up" precedent IsWordByte/
// FormatTextEdit themselves already set.
bool IsFormatWhitespace(char c);

// The literal leading-whitespace substring of the line containing byte
// offset `at` -- reused verbatim (not recomputed from a column) so a mixed
// tabs/spaces header's own indent survives untouched, matching
// NextLineIndented's own "one level deeper than whatever's already there"
// contract rather than a from-scratch column recomputation.
std::string_view LineIndentOf(std::string_view text, std::size_t at);

// align/arrange-kind follow-up: the byte offset the line containing `at`
// itself starts at -- lifted out of FormatBlankLines.cpp's own private copy
// once a THIRD consumer (FormatAlign.h, then FormatArrange.h) needed the
// exact same one-liner, the same "extract once a real second [here, third]
// use shows up" precedent this file's own header comment already set for
// IsWordByte/FormatTextEdit.
std::size_t LineStartOf(std::string_view text, std::size_t at);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATEDIT_H
