//
// The host protocol's payload codec: CBOR (RFC 8949) through nlohmann's
// codec, so a message is the same Json value every other protocol here
// builds, and file contents ride as native byte strings (Json::binary_t).
//
// Decoding treats its input as untrusted: nesting is capped, because the
// decoder recurses once per level and a frame of nothing but array headers
// would otherwise run the stack out.
//

#ifndef NED_EDITOR_PROTOCOL_CBOR_H
#define NED_EDITOR_PROTOCOL_CBOR_H

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace ned::editor::protocol {

inline constexpr std::size_t kMaxCborDepth = 256;

class CborError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string EncodeCbor(const nlohmann::json& value);

// Throws CborError on malformed input, trailing bytes, a tag, or nesting
// deeper than kMaxCborDepth.
[[nodiscard]] nlohmann::json DecodeCbor(std::string_view bytes);

} // namespace ned::editor::protocol

#endif // NED_EDITOR_PROTOCOL_CBOR_H
