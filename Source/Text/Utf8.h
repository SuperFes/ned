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

} // namespace ned::text

#endif // NED_TEXT_UTF8_H
