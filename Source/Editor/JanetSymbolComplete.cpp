#include "JanetSymbolComplete.h"

#include <cctype>

namespace ned::editor {

namespace {
    bool IsJanetSymbolChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '/';
    }
} // namespace

std::size_t JanetSymbolPrefixStart(std::string_view content, std::size_t point) {
    std::size_t start = point;
    while (start > 0 && IsJanetSymbolChar(content[start - 1])) {
        --start;
    }
    return start;
}

} // namespace ned::editor
