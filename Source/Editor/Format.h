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

struct Mode;

// Applies every enabled Hygiene rule to buffer's whole content, in the same
// order Buffer::SaveToFile's own pipeline uses (trim -> blank-line collapse
// -> final newline) so the two never disagree about what "clean" looks
// like. A genuine no-op (content already clean, or every rule disabled)
// touches the buffer/undo tree not at all. Returns whether anything
// changed.
bool ApplyHygienePass(text::Buffer& buffer);

// format-buffer's own Native tier, whole-buffer: the per-language reindent,
// then each configured rule kind in the one order they compose in, then
// Hygiene -- all as a single undo step. Returns whether anything changed.
//
// A free function rather than the body of format-buffer's lambda because two
// call sites need it: the command itself, and BufferView's LSP-format
// callback, which falls back here when a server that claimed the tier
// returns nothing usable. `mode` may be null (a buffer with no major mode
// still gets Hygiene).
//
// Ordering, which is load-bearing and not alphabetical: Rewrite first (a
// pure in-place content swap that changes no line layout, so "fix content
// equivalences before deciding layout" reads as the obvious story), then
// Arrange (reordering whole import lines changes which lines are adjacent to
// which -- exactly the fact Blank's rules must be settled against, not react
// to mid-reorder), then Blank (its edit region ends at a capture's startByte
// and sits strictly above that capture's line, never overlapping a Break/
// Wrap/Space region for any capture this codebase names), then Wrap (it
// rewrites a list's interior line layout wholesale, which is the structural
// change Break's brace-placement gap and Space's token-adjacency checks need
// to see the result of), then Break, then Space, then Align last (its column
// computation must read wherever an anchor ended up AFTER Space's rules ran,
// or its padding is undone or doubled by them). Every pass after the first
// re-reads a fresh capture list from the buffer's own text: an earlier pass
// may have shifted every byte offset after its own edits.
bool ApplyNativeFormat(text::Buffer& buffer, const Mode* mode);

} // namespace ned::editor

#endif // NED_EDITOR_FORMAT_H
