#include "Base64.h"

#include <cstdint>

namespace ned::text {

std::string Base64Encode(std::string_view data) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const auto b0 = static_cast<unsigned char>(data[i]);
        const auto b1 = static_cast<unsigned char>(data[i + 1]);
        const auto b2 = static_cast<unsigned char>(data[i + 2]);
        result += kAlphabet[b0 >> 2];
        result += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
        result += kAlphabet[((b1 & 0x0F) << 2) | (b2 >> 6)];
        result += kAlphabet[b2 & 0x3F];
    }

    const std::size_t remaining = data.size() - i;
    if (remaining == 1) {
        const auto b0 = static_cast<unsigned char>(data[i]);
        result += kAlphabet[b0 >> 2];
        result += kAlphabet[(b0 & 0x03) << 4];
        result += "==";
    }
    else if (remaining == 2) {
        const auto b0 = static_cast<unsigned char>(data[i]);
        const auto b1 = static_cast<unsigned char>(data[i + 1]);
        result += kAlphabet[b0 >> 2];
        result += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
        result += kAlphabet[(b1 & 0x0F) << 2];
        result += '=';
    }
    return result;
}

std::optional<std::string> Base64Decode(std::string_view text) {
    std::string   result;
    std::uint32_t bits    = 0;
    int           count   = 0;
    bool          padding = false;
    result.reserve(text.size() / 4 * 3);
    for (const char c : text) {
        int value = -1;
        if (c >= 'A' && c <= 'Z') {
            value = c - 'A';
        }
        else if (c >= 'a' && c <= 'z') {
            value = c - 'a' + 26;
        }
        else if (c >= '0' && c <= '9') {
            value = c - '0' + 52;
        }
        else if (c == '+') {
            value = 62;
        }
        else if (c == '/') {
            value = 63;
        }
        else if (c == '=') {
            padding = true;
            continue;
        }
        else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }
        if (value < 0 || padding) {
            return std::nullopt;
        }
        bits = (bits << 6) | static_cast<std::uint32_t>(value);
        if (++count == 4) {
            result += static_cast<char>((bits >> 16) & 0xFF);
            result += static_cast<char>((bits >> 8) & 0xFF);
            result += static_cast<char>(bits & 0xFF);
            bits  = 0;
            count = 0;
        }
    }
    if (count == 1) {
        return std::nullopt;
    }
    if (count == 2) {
        result += static_cast<char>((bits >> 4) & 0xFF);
    }
    else if (count == 3) {
        result += static_cast<char>((bits >> 10) & 0xFF);
        result += static_cast<char>((bits >> 2) & 0xFF);
    }
    return result;
}

} // namespace ned::text
