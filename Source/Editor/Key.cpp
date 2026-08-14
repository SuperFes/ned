#include "Key.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ned::editor {

namespace {

std::optional<char32_t> DecodeSingleCodepoint(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    const auto b0 = static_cast<unsigned char>(text[0]);
    std::size_t len;
    char32_t    cp;

    if (b0 < 0x80) {
        len = 1;
        cp  = b0;
    } else if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        cp  = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        cp  = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        cp  = b0 & 0x07;
    } else {
        return std::nullopt;
    }

    if (text.size() != len) {
        return std::nullopt; // must be exactly one codepoint, no more, no less
    }

    for (std::size_t i = 1; i < len; ++i) {
        const auto b = static_cast<unsigned char>(text[i]);
        if ((b & 0xC0) != 0x80) {
            return std::nullopt;
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    return cp;
}

const std::unordered_map<std::string_view, SpecialKey>& NamedKeys() {
    static const std::unordered_map<std::string_view, SpecialKey> table = {
        {"RET", SpecialKey::Enter},     {"TAB", SpecialKey::Tab},        {"DEL", SpecialKey::Backspace},
        {"ESC", SpecialKey::Escape},    {"UP", SpecialKey::Up},          {"DOWN", SpecialKey::Down},
        {"LEFT", SpecialKey::Left},     {"RIGHT", SpecialKey::Right},    {"HOME", SpecialKey::Home},
        {"END", SpecialKey::End},       {"PRIOR", SpecialKey::PageUp},   {"PAGEUP", SpecialKey::PageUp},
        {"NEXT", SpecialKey::PageDown}, {"PAGEDOWN", SpecialKey::PageDown},
        {"F1", SpecialKey::F1},         {"F2", SpecialKey::F2},         {"F3", SpecialKey::F3},
        {"F4", SpecialKey::F4},         {"F5", SpecialKey::F5},         {"F6", SpecialKey::F6},
        {"F7", SpecialKey::F7},         {"F8", SpecialKey::F8},         {"F9", SpecialKey::F9},
        {"F10", SpecialKey::F10},       {"F11", SpecialKey::F11},       {"F12", SpecialKey::F12},
    };
    return table;
}

} // namespace

KeyChord ParseKeyChord(std::string_view token) {
    KeyChord chord;

    while (token.size() >= 2 && token[1] == '-' && (token[0] == 'C' || token[0] == 'M' || token[0] == 'S')) {
        switch (token[0]) {
            case 'C':
                chord.Control = true;
                break;
            case 'M':
                chord.Meta = true;
                break;
            case 'S':
                chord.Shift = true;
                break;
            default:
                break;
        }
        token.remove_prefix(2);
    }

    if (token.empty()) {
        throw std::invalid_argument("ned: empty key token");
    }

    if (token == "SPC") {
        chord.Codepoint = U' ';
        return chord;
    }

    if (const auto it = NamedKeys().find(token); it != NamedKeys().end()) {
        chord.Special = it->second;
        return chord;
    }

    if (const auto codepoint = DecodeSingleCodepoint(token)) {
        chord.Codepoint = *codepoint;
        return chord;
    }

    throw std::invalid_argument("ned: unrecognized key token: " + std::string(token));
}

std::vector<KeyChord> ParseKeySequence(std::string_view text) {
    std::vector<KeyChord> sequence;

    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') {
            ++pos;
        }
        if (pos >= text.size()) {
            break;
        }

        const std::size_t start = pos;
        while (pos < text.size() && text[pos] != ' ') {
            ++pos;
        }

        sequence.push_back(ParseKeyChord(text.substr(start, pos - start)));
    }

    return sequence;
}

} // namespace ned::editor
