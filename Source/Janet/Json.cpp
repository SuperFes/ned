#include "Json.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace ned::janet {

namespace {

    Janet StringValue(const std::string& text) {
        return janet_stringv(reinterpret_cast<const std::uint8_t*>(text.data()), static_cast<std::int32_t>(text.size()));
    }

    Janet Convert(const nlohmann::json& value) {
        switch (value.type()) {
            case nlohmann::json::value_t::object: {
                JanetTable* table = janet_table(static_cast<std::int32_t>(value.size()));
                for (const auto& [key, member] : value.items()) {
                    if (member.is_null()) {
                        continue;
                    }
                    janet_table_put(table,
                                    janet_keywordv(reinterpret_cast<const std::uint8_t*>(key.data()), static_cast<std::int32_t>(key.size())),
                                    Convert(member));
                }
                return janet_wrap_table(table);
            }
            case nlohmann::json::value_t::array: {
                JanetArray* array = janet_array(static_cast<std::int32_t>(value.size()));
                for (const nlohmann::json& element : value) {
                    janet_array_push(array, Convert(element));
                }
                return janet_wrap_array(array);
            }
            case nlohmann::json::value_t::string:
                return StringValue(value.get_ref<const std::string&>());
            case nlohmann::json::value_t::boolean:
                return janet_wrap_boolean(value.get<bool>());
            case nlohmann::json::value_t::number_integer:
                return janet_wrap_number(static_cast<double>(value.get<std::int64_t>()));
            case nlohmann::json::value_t::number_unsigned:
                return janet_wrap_number(static_cast<double>(value.get<std::uint64_t>()));
            case nlohmann::json::value_t::number_float:
                return janet_wrap_number(value.get<double>());
            case nlohmann::json::value_t::null:
            case nlohmann::json::value_t::binary:
            case nlohmann::json::value_t::discarded:
                break;
        }
        return janet_wrap_nil();
    }

    std::string BytesOf(Janet value) {
        const std::uint8_t* bytes = janet_unwrap_string(value);
        return std::string(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(janet_string_length(bytes)));
    }

    nlohmann::json ToJson(Janet value, int depth) {
        constexpr int kMaxDepth = 256;
        if (depth > kMaxDepth) {
            throw std::runtime_error("ned: json-encode: nested too deeply");
        }
        switch (janet_type(value)) {
            case JANET_NIL:
                return nullptr;
            case JANET_BOOLEAN:
                return janet_unwrap_boolean(value) != 0;
            case JANET_NUMBER: {
                const double     number         = janet_unwrap_number(value);
                constexpr double kExactIntegers = 9007199254740992.0; // 2^53
                if (std::isfinite(number) && std::trunc(number) == number && std::abs(number) <= kExactIntegers) {
                    return static_cast<std::int64_t>(number);
                }
                if (!std::isfinite(number)) {
                    throw std::runtime_error("ned: json-encode: JSON has no infinity or NaN");
                }
                return number;
            }
            case JANET_STRING:
            case JANET_KEYWORD:
            case JANET_SYMBOL:
                return BytesOf(value);
            case JANET_ARRAY:
            case JANET_TUPLE: {
                const Janet*   items = nullptr;
                std::int32_t   count = 0;
                nlohmann::json array = nlohmann::json::array();
                janet_indexed_view(value, &items, &count);
                for (std::int32_t i = 0; i < count; ++i) {
                    array.push_back(ToJson(items[i], depth + 1));
                }
                return array;
            }
            case JANET_TABLE:
            case JANET_STRUCT: {
                const JanetKV* entries  = nullptr;
                std::int32_t   count    = 0;
                std::int32_t   capacity = 0;
                nlohmann::json object   = nlohmann::json::object();
                janet_dictionary_view(value, &entries, &count, &capacity);
                for (std::int32_t i = 0; i < capacity; ++i) {
                    const Janet key = entries[i].key;
                    if (janet_checktype(key, JANET_NIL)) {
                        continue;
                    }
                    if (!janet_checktypes(key, JANET_TFLAG_BYTES) || janet_checktype(key, JANET_BUFFER)) {
                        throw std::runtime_error("ned: json-encode: an object key must be a keyword, string or symbol");
                    }
                    object[BytesOf(key)] = ToJson(entries[i].value, depth + 1);
                }
                return object;
            }
            default:
                throw std::runtime_error("ned: json-encode: can't write a " +
                                         std::string(reinterpret_cast<const char*>(janet_type_names[janet_type(value)])) + " as JSON");
        }
    }

} // namespace

Janet JsonToJanet(std::string_view text) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(text);
    }
    catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(std::string("ned: invalid JSON: ") + e.what());
    }
    return Convert(parsed);
}

std::string JanetToJson(Janet value) {
    return ToJson(value, 0).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace ned::janet
