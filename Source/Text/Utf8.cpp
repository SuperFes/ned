#include "Utf8.h"

#include <algorithm>

namespace ned::text {

std::string EncodeCodepointUtf8(char32_t codepoint) {
    std::string out;

    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }

    return out;
}

char32_t DecodeCodepointUtf8(std::string_view utf8Text, std::size_t offset) {
    if (offset >= utf8Text.size()) {
        return 0xFFFD;
    }

    const auto b0 = static_cast<unsigned char>(utf8Text[offset]);
    if (b0 < 0x80) {
        return static_cast<char32_t>(b0);
    }

    std::size_t len;
    char32_t    cp;
    if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        cp  = b0 & 0x1F;
    }
    else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        cp  = b0 & 0x0F;
    }
    else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        cp  = b0 & 0x07;
    }
    else {
        return 0xFFFD;
    }

    if (offset + len > utf8Text.size()) {
        return 0xFFFD;
    }

    for (std::size_t i = 1; i < len; ++i) {
        const auto b = static_cast<unsigned char>(utf8Text[offset + i]);
        if ((b & 0xC0) != 0x80) {
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    return cp;
}

std::size_t NextCodepointBoundary(std::string_view utf8Text, std::size_t offset) {
    if (offset >= utf8Text.size()) {
        return utf8Text.size();
    }

    std::size_t next = offset + 1;
    while (next < utf8Text.size() && (static_cast<unsigned char>(utf8Text[next]) & 0xC0) == 0x80) {
        ++next;
    }
    return next;
}

std::size_t PreviousCodepointBoundary(std::string_view utf8Text, std::size_t offset) {
    if (offset == 0) {
        return 0;
    }

    std::size_t prev = std::min(offset, utf8Text.size()) - 1;
    while (prev > 0 && (static_cast<unsigned char>(utf8Text[prev]) & 0xC0) == 0x80) {
        --prev;
    }
    return prev;
}

std::size_t SnapDownToCodepointBoundary(std::string_view utf8Text, std::size_t offset) {
    std::size_t pos = std::min(offset, utf8Text.size());
    for (int steps = 0; pos > 0 && pos < utf8Text.size() && steps < 3 && (static_cast<unsigned char>(utf8Text[pos]) & 0xC0) == 0x80;
         ++steps) {
        --pos;
    }
    return pos;
}

std::size_t SnapUpToCodepointBoundary(std::string_view utf8Text, std::size_t offset) {
    std::size_t pos = std::min(offset, utf8Text.size());
    for (int steps = 0; pos < utf8Text.size() && steps < 3 && (static_cast<unsigned char>(utf8Text[pos]) & 0xC0) == 0x80; ++steps) {
        ++pos;
    }
    return pos;
}

void RemoveLastCodepoint(std::string& utf8Text) {
    if (utf8Text.empty()) {
        return;
    }

    utf8Text.erase(PreviousCodepointBoundary(utf8Text, utf8Text.size()));
}

} // namespace ned::text
