#include "KeyTranslation.h"

#include <cstdint>

namespace ned::ui {

std::optional<editor::KeyChord> TranslateKey(esc::Key key) {
    using editor::KeyChord;
    using editor::SpecialKey;

    switch (key) {
        case esc::Key::Tab:
            return KeyChord{.Special = SpecialKey::Tab};
        case esc::Key::BackTab:
            return KeyChord{.Shift = true, .Special = SpecialKey::Tab};
        case esc::Key::Enter:
            return KeyChord{.Special = SpecialKey::Enter};
        case esc::Key::Escape:
            return KeyChord{.Special = SpecialKey::Escape};
        case esc::Key::Backspace:
        case esc::Key::Backspace1:
        case esc::Key::Backspace2:
            return KeyChord{.Special = SpecialKey::Backspace};
        case esc::Key::Delete:
            return KeyChord{.Special = SpecialKey::Delete};
        case esc::Key::Home:
            return KeyChord{.Special = SpecialKey::Home};
        case esc::Key::End:
            return KeyChord{.Special = SpecialKey::End};
        case esc::Key::PageUp:
            return KeyChord{.Special = SpecialKey::PageUp};
        case esc::Key::PageDown:
            return KeyChord{.Special = SpecialKey::PageDown};
        case esc::Key::ArrowUp:
            return KeyChord{.Special = SpecialKey::Up};
        case esc::Key::ArrowDown:
            return KeyChord{.Special = SpecialKey::Down};
        case esc::Key::ArrowLeft:
            return KeyChord{.Special = SpecialKey::Left};
        case esc::Key::ArrowRight:
            return KeyChord{.Special = SpecialKey::Right};
        case esc::Key::Function1:
            return KeyChord{.Special = SpecialKey::F1};
        case esc::Key::Function2:
            return KeyChord{.Special = SpecialKey::F2};
        case esc::Key::Function3:
            return KeyChord{.Special = SpecialKey::F3};
        case esc::Key::Function4:
            return KeyChord{.Special = SpecialKey::F4};
        case esc::Key::Function5:
            return KeyChord{.Special = SpecialKey::F5};
        case esc::Key::Function6:
            return KeyChord{.Special = SpecialKey::F6};
        case esc::Key::Function7:
            return KeyChord{.Special = SpecialKey::F7};
        case esc::Key::Function8:
            return KeyChord{.Special = SpecialKey::F8};
        case esc::Key::Function9:
            return KeyChord{.Special = SpecialKey::F9};
        case esc::Key::Function10:
            return KeyChord{.Special = SpecialKey::F10};
        case esc::Key::Function11:
            return KeyChord{.Special = SpecialKey::F11};
        case esc::Key::Function12:
            return KeyChord{.Special = SpecialKey::F12};
        default:
            break;
    }

    const auto raw = static_cast<std::uint32_t>(key);

    // C0 control codes 1-26 are Ctrl+a..Ctrl+z. The named ones among them
    // (Tab=9, Enter=13) were already handled by the switch above and never
    // reach here.
    if (raw >= 1 && raw <= 26) {
        return KeyChord{.Control = true, .Codepoint = static_cast<char32_t>('a' + raw - 1)};
    }

    // Graphic characters and general Unicode codepoints -- Key's underlying
    // value is the literal codepoint for anything printable. 0x40000 and up
    // are raw-mode-only modifier/lock-key sentinels (LCtrl, CapsLock, ...),
    // not real codepoints, even though they're numerically within range.
    if (raw >= 32 && raw < 0x40000) {
        return KeyChord{.Codepoint = static_cast<char32_t>(raw)};
    }

    return std::nullopt; // unmapped control code (Null, FileSeparator, ...) or raw-mode-only key
}

} // namespace ned::ui
