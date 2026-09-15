//
// configurable-formatter follow-up. The Hygiene pass: trailing-whitespace
// strip, blank-line-run collapse, and a final newline, applied directly to
// a live Buffer's own content as one undo step -- unlike the identically
// named settings that already run at *save* time (TrimOnSave.h/
// FinalNewline.h, still unmodified and still the default for an ordinary
// save-buffer), this is what lets format-buffer show the cleaned-up result
// immediately, without requiring a save first (format-buffer's own
// contract is "without saving" -- see Commands.cpp).
//
// Deliberately whole-buffer only for now (matches format-buffer's own
// existing whole-buffer-replace convention for its External/Native chain);
// huge-file windowing is out of scope here, same as the Native indent path
// -- see ROADMAP.md's "Huge-file streaming sweep" follow-up.
//
// Each of the three rules is independently gated by its own existing
// setting -- TrimOnSave.h's TrimTrailingWhitespaceOnSave(),
// MaxConsecutiveBlankLines.h's MaxConsecutiveBlankLines(), FinalNewline.h's
// EnsureFinalNewline() -- so a user who disabled one of those for saving
// doesn't have format-buffer quietly re-enable it. The actual per-rule text
// transforms are Text/WhitespaceHygiene.h's pure functions, the same ones
// Buffer::SaveToFile's own disk-only default calls -- one implementation,
// two call sites (see that header's own doc comment for why the huge-file
// streaming save path is the one deliberate exception).
//

#ifndef NED_EDITOR_FORMAT_H
#define NED_EDITOR_FORMAT_H

#include "Text/Buffer.h"

namespace ned::editor {

// Applies every enabled Hygiene rule to buffer's whole content, in the same
// order Buffer::SaveToFile's own pipeline uses (trim -> blank-line collapse
// -> final newline) so the two never disagree about what "clean" looks
// like. A genuine no-op (content already clean, or every rule disabled)
// touches the buffer/undo tree not at all. Returns whether anything
// changed.
bool ApplyHygienePass(text::Buffer& buffer);

} // namespace ned::editor

#endif // NED_EDITOR_FORMAT_H
