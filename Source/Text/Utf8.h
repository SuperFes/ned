//
// Small UTF-8 encoding helper shared by anything that needs to turn a single
// Unicode codepoint (e.g. a keystroke) into bytes to insert into a Rope/Buffer.
//

#ifndef NED_TEXT_UTF8_H
#define NED_TEXT_UTF8_H

#include <string>

namespace ned::text {

[[nodiscard]] std::string EncodeCodepointUtf8(char32_t codepoint);

// Removes the last codepoint's worth of bytes from a well-formed UTF-8
// string (e.g. one built exclusively via EncodeCodepointUtf8 appends).
// No-op if empty.
void RemoveLastCodepoint(std::string& utf8Text);

} // namespace ned::text

#endif // NED_TEXT_UTF8_H
