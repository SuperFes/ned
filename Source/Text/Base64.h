//
// Standard base64 (RFC 4648, with padding) -- OSC 52 clipboard writes and
// ACP image blocks.
//

#ifndef NED_TEXT_BASE64_H
#define NED_TEXT_BASE64_H

#include <string>
#include <string_view>

namespace ned::text {

[[nodiscard]] std::string Base64Encode(std::string_view data);

} // namespace ned::text

#endif // NED_TEXT_BASE64_H
