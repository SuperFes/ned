//
// Field access and path encoding shared by the host API's wire forms. Each
// Require* throws protocol::ProtocolError when the field is missing or has
// the wrong type: a peer that sends one is broken, or hostile.
//

#ifndef NED_EDITOR_HOST_CODEC_H
#define NED_EDITOR_HOST_CODEC_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "Editor/Protocol/Endpoint.h"

namespace ned::editor::host {

[[nodiscard]] const nlohmann::json& RequireObject(const nlohmann::json& json, const char* field);
[[nodiscard]] const nlohmann::json& RequireArray(const nlohmann::json& json, const char* field);
[[nodiscard]] std::string           RequireString(const nlohmann::json& json, const char* field);
[[nodiscard]] std::uint64_t         RequireUnsigned(const nlohmann::json& json, const char* field);
// A CBOR byte string, as the bytes it holds.
[[nodiscard]] std::string RequireBytes(const nlohmann::json& json, const char* field);

[[nodiscard]] nlohmann::json Bytes(std::string_view bytes);

// Paths cross as byte strings, because POSIX paths are bytes rather than
// UTF-8. Decoding requires an absolute path with no NUL, lexically
// normalized.
[[nodiscard]] nlohmann::json        PathToJson(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path RequirePath(const nlohmann::json& json, const char* field);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_CODEC_H
