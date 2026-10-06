#include "Codec.h"

#include <vector>

namespace ned::editor::host {

using Json = nlohmann::json;
using protocol::ProtocolError;

namespace {

    const Json& Field(const Json& json, const char* field) {
        const auto it = json.is_object() ? json.find(field) : json.end();
        if (!json.is_object() || it == json.end()) {
            throw ProtocolError(std::string("ned: host message lacks '") + field + "'");
        }
        return *it;
    }

    [[noreturn]] void WrongType(const char* field, const char* expected) {
        throw ProtocolError(std::string("ned: host field '") + field + "' is not " + expected);
    }

} // namespace

const Json& RequireObject(const Json& json, const char* field) {
    const Json& value = Field(json, field);
    if (!value.is_object()) {
        WrongType(field, "a map");
    }
    return value;
}

const Json& RequireArray(const Json& json, const char* field) {
    const Json& value = Field(json, field);
    if (!value.is_array()) {
        WrongType(field, "an array");
    }
    return value;
}

std::string RequireString(const Json& json, const char* field) {
    const Json& value = Field(json, field);
    if (!value.is_string()) {
        WrongType(field, "a string");
    }
    return value.get<std::string>();
}

std::uint64_t RequireUnsigned(const Json& json, const char* field) {
    const Json& value = Field(json, field);
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
        return static_cast<std::uint64_t>(value.get<std::int64_t>());
    }
    WrongType(field, "an unsigned integer");
}

bool RequireBool(const Json& json, const char* field) {
    const Json& value = Field(json, field);
    if (!value.is_boolean()) {
        WrongType(field, "a boolean");
    }
    return value.get<bool>();
}

std::string RequireBytes(const Json& json, const char* field) {
    const Json& value = Field(json, field);
    if (!value.is_binary()) {
        WrongType(field, "a byte string");
    }
    const std::vector<std::uint8_t>& bytes = value.get_binary();
    return {bytes.begin(), bytes.end()};
}

Json Bytes(std::string_view bytes) {
    return Json::binary(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}

Json PathToJson(const std::filesystem::path& path) {
    return Bytes(path.native());
}

std::filesystem::path RequirePath(const Json& json, const char* field) {
    const std::string bytes = RequireBytes(json, field);
    if (bytes.empty() || bytes.front() != '/' || bytes.find('\0') != std::string::npos) {
        throw ProtocolError(std::string("ned: host field '") + field + "' is not an absolute path");
    }
    return std::filesystem::path(bytes).lexically_normal();
}

} // namespace ned::editor::host
