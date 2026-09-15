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

} // namespace ned::editor

#endif // NED_EDITOR_FORMATEDIT_H
