#include "Json.h"

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

} // namespace ned::janet
