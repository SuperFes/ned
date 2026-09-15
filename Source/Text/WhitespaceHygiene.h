//
// configurable-formatter follow-up. Pure, buffer-free whole-document
// whitespace transforms -- extracted out of Buffer.cpp's non-huge
// WriteBufferContent rather than duplicated, so Buffer::SaveToFile's
// disk-only default and Editor::ApplyHygienePass (Editor/Format.h) share
// exactly one implementation of each rule.
//
// Buffer.cpp's HUGE-file streaming write path (StreamingSaveWriter) is
// deliberately a SEPARATE, byte-at-a-time reimplementation of
// TrimTrailingWhitespaceAndBlankLines/EnsureTrailingNewline below, not a
// caller of it: materializing a multi-GB document into one std::string the
// way these functions do defeats the entire point of streaming. The two are
// held byte-identical by Tests/BufferSaveEquivalenceTest.cpp, not just
// assumed -- if you change the algorithm here, that test is what has to
// keep passing.
//

#ifndef NED_TEXT_WHITESPACEHYGIENE_H
#define NED_TEXT_WHITESPACEHYGIENE_H

#include <string>

namespace ned::text {

// Strips trailing spaces/tabs from every line, then collapses any run of
// blank lines at the very end of the document down to nothing --
// EnsureTrailingNewline below is what puts exactly one '\n' back if wanted.
// A no-op on an empty string. (TrimOnSave.h's own doc comment: "remove
// trailing spaces and extra newlines at the end of a file" was raised as a
// single ask, hence one combined function rather than two.)
[[nodiscard]] std::string TrimTrailingWhitespaceAndBlankLines(std::string content);

// Appends '\n' if content is non-empty and doesn't already end with one.
[[nodiscard]] std::string EnsureTrailingNewline(std::string content);

// configurable-formatter Hygiene-pass follow-up: collapses any run of MORE
// than maxConsecutive consecutive blank lines down to exactly
// maxConsecutive, anywhere in the document (not just at the end -- the rule
// TrimTrailingWhitespaceAndBlankLines above already covers on its own). A
// "blank" line is one that is empty or whitespace-only -- checked directly
// here rather than assumed already-trimmed, so this is correct whether or
// not TrimTrailingWhitespaceAndBlankLines ran first (a user can have one
// enabled without the other). maxConsecutive < 0 is a no-op (the sentinel
// Editor::MaxConsecutiveBlankLines() uses for "disabled").
[[nodiscard]] std::string CollapseBlankLineRuns(std::string content, int maxConsecutive);

} // namespace ned::text

#endif // NED_TEXT_WHITESPACEHYGIENE_H
