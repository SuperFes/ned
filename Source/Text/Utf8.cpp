#include "Utf8.h"

namespace ned::text {

std::string EncodeCodepointUtf8(char32_t codepoint) {
    std::string out;

    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }

    return out;
}

void RemoveLastCodepoint(std::string& utf8Text) {
    if (utf8Text.empty()) {
        return;
    }

    std::size_t end = utf8Text.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(utf8Text[end]) & 0xC0) == 0x80) {
        --end;
    }
    utf8Text.erase(end);
}

} // namespace ned::text
