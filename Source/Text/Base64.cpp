#include "Base64.h"

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

} // namespace ned::text
