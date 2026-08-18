#include "Value.h"

#include <stdexcept>

namespace ned::janet {

template <>
bool FromJanet<bool>(Janet value) {
    if (!janet_checktype(value, JANET_BOOLEAN)) {
        throw std::runtime_error("ned: expected a boolean");
    }
    return janet_unwrap_boolean(value) != 0;
}

template <>
std::int64_t FromJanet<std::int64_t>(Janet value) {
    if (!janet_checktype(value, JANET_NUMBER)) {
        throw std::runtime_error("ned: expected a number");
    }
    return static_cast<std::int64_t>(janet_unwrap_number(value));
}

template <>
std::size_t FromJanet<std::size_t>(Janet value) {
    if (!janet_checktype(value, JANET_NUMBER)) {
        throw std::runtime_error("ned: expected a number");
    }
    const double number = janet_unwrap_number(value);
    if (number < 0) {
        throw std::runtime_error("ned: expected a non-negative number");
    }
    return static_cast<std::size_t>(number);
}

template <>
double FromJanet<double>(Janet value) {
    if (!janet_checktype(value, JANET_NUMBER)) {
        throw std::runtime_error("ned: expected a number");
    }
    return janet_unwrap_number(value);
}

template <>
std::string FromJanet<std::string>(Janet value) {
    if (!janet_checktype(value, JANET_STRING)) {
        throw std::runtime_error("ned: expected a string");
    }
    const JanetString str = janet_unwrap_string(value);
    return std::string(reinterpret_cast<const char*>(str), janet_string_length(str));
}

template <>
std::vector<std::string> FromJanet<std::vector<std::string>>(Janet value) {
    const Janet* items = nullptr;
    std::int32_t count = 0;
    if (!janet_indexed_view(value, &items, &count)) {
        throw std::runtime_error("ned: expected an array or tuple of strings");
    }
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        result.push_back(FromJanet<std::string>(items[i]));
    }
    return result;
}

template <>
Janet FromJanet<Janet>(Janet value) {
    return value;
}

template <>
RootedValue FromJanet<RootedValue>(Janet value) {
    if (!janet_checktype(value, JANET_FUNCTION) && !janet_checktype(value, JANET_CFUNCTION)) {
        throw std::runtime_error("ned: expected a function");
    }
    return RootedValue(value);
}

Janet ToJanet(bool value) {
    return janet_wrap_boolean(value ? 1 : 0);
}

Janet ToJanet(std::int64_t value) {
    return janet_wrap_number(static_cast<double>(value));
}

Janet ToJanet(std::size_t value) {
    return janet_wrap_number(static_cast<double>(value));
}

Janet ToJanet(double value) {
    return janet_wrap_number(value);
}

Janet ToJanet(const std::string& value) {
    return janet_wrap_string(janet_string(reinterpret_cast<const std::uint8_t*>(value.data()), static_cast<std::int32_t>(value.size())));
}

Janet ToJanet(const std::optional<std::string>& value) {
    return value ? ToJanet(*value) : janet_wrap_nil();
}

Janet ToJanet(const std::optional<bool>& value) {
    return value ? ToJanet(*value) : janet_wrap_nil();
}

Janet ToJanet(const std::vector<std::string>& value) {
    JanetArray* array = janet_array(static_cast<std::int32_t>(value.size()));
    for (const std::string& item : value) {
        janet_array_push(array, ToJanet(item));
    }
    return janet_wrap_array(array);
}

RootedValue::Root::Root(Janet v) : value(v) {
    janet_gcroot(value);
}

RootedValue::Root::~Root() {
    janet_gcunroot(value);
}

RootedValue::RootedValue(Janet value) : root_(std::make_shared<Root>(value)) {
}

Janet RootedValue::Get() const {
    return root_->value;
}

} // namespace ned::janet
