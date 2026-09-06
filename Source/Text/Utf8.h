//
// Small UTF-8 encoding helper shared by anything that needs to turn a single
// Unicode codepoint (e.g. a keystroke) into bytes to insert into a Rope/Buffer.
//

#ifndef NED_TEXT_UTF8_H
#define NED_TEXT_UTF8_H

#include <cstddef>
#include <string>
#include <string_view>

namespace ned::text {

[[nodiscard]] std::string EncodeCodepointUtf8(char32_t codepoint);

// Removes the last codepoint's worth of bytes from a well-formed UTF-8
// string (e.g. one built exclusively via EncodeCodepointUtf8 appends).
// No-op if empty.
void RemoveLastCodepoint(std::string& utf8Text);

// The byte offset just past the codepoint starting at offset: one byte
// forward, then past any continuation bytes. Clamps to utf8Text's size;
// returns size if offset is already at or past the end. Tolerates malformed
// sequences (never advances past a lead byte, never returns less than
// offset + 1 unless already at the end) -- used for "advance one codepoint"
// stepping over content that is usually, but not guaranteedly, valid UTF-8
// (in-file regex replace steps over whatever bytes a text-looking file holds).
[[nodiscard]] std::size_t NextCodepointBoundary(std::string_view utf8Text, std::size_t offset);

// The byte offset of the codepoint immediately before offset: one byte back,
// then back past any continuation bytes. Clamps to 0; a no-op (returns
// offset) if offset is already 0. Same malformed-sequence tolerance as
// NextCodepointBoundary -- never retreats past a lead byte.
[[nodiscard]] std::size_t PreviousCodepointBoundary(std::string_view utf8Text, std::size_t offset);

// Clamps an arbitrary/untrusted byte offset -- one not known to already sit
// on a codepoint boundary, e.g. produced by a byte-level diff that isn't
// itself UTF-8-aware -- backward to the start of whichever codepoint it
// falls inside. A no-op if offset is already a boundary (including
// offset == utf8Text.size()). Unlike PreviousCodepointBoundary (which always
// steps back one whole codepoint, for "delete the codepoint before the
// cursor"-style callers that know their input is already aligned), this
// never moves an already-aligned offset at all. Bounded to at most 3 steps
// back (the longest possible run of UTF-8 continuation bytes), so malformed
// input can't walk it arbitrarily far.
[[nodiscard]] std::size_t SnapDownToCodepointBoundary(std::string_view utf8Text, std::size_t offset);

// The forward-snapping counterpart: moves offset forward past the
// remaining continuation bytes of whichever codepoint it falls inside. A
// no-op if offset is already a boundary. Same malformed-input bound as
// SnapDownToCodepointBoundary.
[[nodiscard]] std::size_t SnapUpToCodepointBoundary(std::string_view utf8Text, std::size_t offset);

} // namespace ned::text

#endif // NED_TEXT_UTF8_H
