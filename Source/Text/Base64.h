//
// Standard base64 (RFC 4648, with padding) -- OSC 52 clipboard writes and
// ACP image blocks both ways.
//

#ifndef NED_TEXT_BASE64_H
#define NED_TEXT_BASE64_H

#include <optional>
#include <string>
#include <string_view>

namespace ned::text {

[[nodiscard]] std::string Base64Encode(std::string_view data);

// The bytes `text` encodes, skipping whitespace; nullopt when anything else
// isn't base64.
[[nodiscard]] std::optional<std::string> Base64Decode(std::string_view text);

} // namespace ned::text

#endif // NED_TEXT_BASE64_H
