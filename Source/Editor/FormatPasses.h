//
// The Native format chain, as data: which capture-driven passes run, and in
// what order.
//
// This exists because the list was hand-copied. `format-buffer`, `ned
// --format` and the scoped format-on-save path each carried their own
// transcription of the same seven calls, each with a comment asserting it
// matched the others -- and by 2026-09-21 two of the three were wrong. The
// CLI had silently fallen three kinds behind (Rewrite, Arrange, Align never
// ran headlessly) and the on-save path missed the keyword-break pass the day
// it shipped. Neither was detectable by reading any one file, because each
// file looked complete and said so.
//
// Callers differ in how they APPLY an edit list -- whole-buffer, or declining
// anything straddling a touched-line scope -- which is why this is a table of
// compute functions rather than one function that does the whole job. The
// order is the thing that has to be shared, and it is load-bearing; see
// Editor/Format.h's ApplyNativeFormat for why each pass sits where it does.
//

#ifndef NED_EDITOR_FORMATPASSES_H
#define NED_EDITOR_FORMATPASSES_H

#include <span>
#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

// Every capture-driven pass has this shape: pure text in, edits out.
using FormatPassFn = std::vector<FormatTextEdit> (*)(std::string_view, std::string_view,
                                                     const std::vector<FormatCapture>&);

struct FormatPass {
    std::string_view name; // for diagnostics and for the order test
    FormatPassFn     compute;
};

// In chain order. Indent runs before all of these and Hygiene after, neither
// being capture-driven; both belong to ApplyNativeFormat, not here.
[[nodiscard]] std::span<const FormatPass> NativeFormatPasses();

} // namespace ned::editor

#endif // NED_EDITOR_FORMATPASSES_H
