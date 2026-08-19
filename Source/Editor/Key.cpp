#include "Key.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "Text/Utf8.h"

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
        {"DELETE", SpecialKey::Delete}, {"ESC", SpecialKey::Escape},    {"UP", SpecialKey::Up},          {"DOWN", SpecialKey::Down},
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

std::string FormatKeyChord(const KeyChord& chord) {
    std::string text;
    if (chord.Control) {
        text += "C-";
    }
    if (chord.Meta) {
        text += "M-";
    }
    if (chord.Shift) {
        text += "S-";
    }

    switch (chord.Special) {
        case SpecialKey::None:
            break;
        case SpecialKey::Enter:
            return text + "RET";
        case SpecialKey::Tab:
            return text + "TAB";
        case SpecialKey::Backspace:
            return text + "DEL";
        case SpecialKey::Delete:
            return text + "DELETE";
        case SpecialKey::Escape:
            return text + "ESC";
        case SpecialKey::Up:
            return text + "UP";
        case SpecialKey::Down:
            return text + "DOWN";
        case SpecialKey::Left:
            return text + "LEFT";
        case SpecialKey::Right:
            return text + "RIGHT";
        case SpecialKey::Home:
            return text + "HOME";
        case SpecialKey::End:
            return text + "END";
        case SpecialKey::PageUp:
            return text + "PAGEUP";
        case SpecialKey::PageDown:
            return text + "PAGEDOWN";
        case SpecialKey::F1:
            return text + "F1";
        case SpecialKey::F2:
            return text + "F2";
        case SpecialKey::F3:
            return text + "F3";
        case SpecialKey::F4:
            return text + "F4";
        case SpecialKey::F5:
            return text + "F5";
        case SpecialKey::F6:
            return text + "F6";
        case SpecialKey::F7:
            return text + "F7";
        case SpecialKey::F8:
            return text + "F8";
        case SpecialKey::F9:
            return text + "F9";
        case SpecialKey::F10:
            return text + "F10";
        case SpecialKey::F11:
            return text + "F11";
        case SpecialKey::F12:
            return text + "F12";
    }

    if (chord.Codepoint == U' ') {
        return text + "SPC";
    }
    return text + text::EncodeCodepointUtf8(chord.Codepoint);
}

std::string FormatKeySequence(const std::vector<KeyChord>& chords) {
    std::string text;
    for (const KeyChord& chord : chords) {
        if (!text.empty()) {
            text += ' ';
        }
        text += FormatKeyChord(chord);
    }
    return text;
}

} // namespace ned::editor
