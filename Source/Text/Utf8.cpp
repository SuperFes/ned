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
