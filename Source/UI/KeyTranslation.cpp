#include "KeyTranslation.h"

#include <cstdint>
#include <string_view>

namespace ned::ui {

namespace {

    using editor::KeyChord;
    using editor::SpecialKey;

    // Decodes a "base" key (no Meta applied) from raw input bytes: a single
    // C0 control byte 1-26 is Ctrl+<letter>, the same convention every
    // terminal uses (confirmed against real byte sequences during the
    // migration's pre-work spike) -- byte 8 is included alongside the
    // standard DEL (127, handled separately by the named Event::Backspace
    // check before this ever runs) since some terminals send one, some the
    // other, for the same physical Backspace key; the pre-migration
    // TermOx/escape-backed translator tolerated both (Backspace1/
    // Backspace2) and this preserves that. Anything else is decoded as one
    // UTF-8 codepoint -- Ned's own KeyChord doesn't track Shift separately
    // for printable characters (a capital letter's codepoint already
    // encodes it), matching the pre-migration translator's behavior.
    std::optional<KeyChord> DecodeBaseKey(std::string_view bytes) {
        if (bytes.empty()) {
            return std::nullopt;
        }

        const auto b0 = static_cast<std::uint8_t>(bytes[0]);

        if (bytes.size() == 1 && b0 == 8) {
            return KeyChord{.Special = SpecialKey::Backspace};
        }
        if (bytes.size() == 1 && b0 >= 1 && b0 <= 26) {
            return KeyChord{.Control = true, .Codepoint = static_cast<char32_t>('a' + b0 - 1)};
        }
        // Byte 0x1F (US, "Unit Separator") is what a real terminal actually
        // sends for Ctrl+_ -- and, since terminals don't distinguish Shift
        // on top of a control byte, for a physical Ctrl+/ press too (same
        // key, unshifted vs shifted). Real Emacs' own undo binding is C-_
        // for exactly this reason; decoded as Control+'_' here to match,
        // confirmed against a real terminal after C-/ (parsed as a literal
        // Control+'/' KeyChord, which no real terminal byte can ever
        // produce) turned out to be dead in practice.
        if (bytes.size() == 1 && b0 == 0x1F) {
            return KeyChord{.Control = true, .Codepoint = U'_'};
        }

        // Standard UTF-8 decode of the leading codepoint -- FTXUI hands us
        // raw encoded bytes here (unlike the old esc::Key, whose value was
        // already a decoded codepoint), so this step is new.
        char32_t    codepoint = 0;
        std::size_t length    = 0;
        if (b0 < 0x80) {
            codepoint = b0;
            length    = 1;
        }
        else if ((b0 & 0xE0) == 0xC0 && bytes.size() >= 2) {
            codepoint = b0 & 0x1F;
            length    = 2;
        }
        else if ((b0 & 0xF0) == 0xE0 && bytes.size() >= 3) {
            codepoint = b0 & 0x0F;
            length    = 3;
        }
        else if ((b0 & 0xF8) == 0xF0 && bytes.size() >= 4) {
            codepoint = b0 & 0x07;
            length    = 4;
        }
        else {
            return std::nullopt; // malformed or an unrecognized multi-byte control sequence
        }

        for (std::size_t i = 1; i < length; ++i) {
            const auto continuation = static_cast<std::uint8_t>(bytes[i]);
            if ((continuation & 0xC0) != 0x80) {
                return std::nullopt;
            }
            codepoint = static_cast<char32_t>((codepoint << 6) | (continuation & 0x3F));
        }
        return KeyChord{.Codepoint = codepoint};
    }

} // namespace

std::optional<KeyChord> TranslateKey(const ftxui::Event& event) {
    if (event.is_mouse()) {
        return std::nullopt;
    }

    // Named multi-byte sequences -- arrows (plain and Ctrl+), navigation
    // keys, and function keys -- matched directly against FTXUI's own
    // pre-parsed constants rather than hand-decoding CSI/SS3 escape
    // sequences ourselves. Ctrl+Arrow support is new (the old translator
    // had no equivalent esc::Key cases for it) -- a real, additive upgrade,
    // not a parity requirement, and effectively free since FTXUI already
    // hands it to us pre-parsed.
    if (event == ftxui::Event::ArrowUp) return KeyChord{.Special = SpecialKey::Up};
    if (event == ftxui::Event::ArrowDown) return KeyChord{.Special = SpecialKey::Down};
    if (event == ftxui::Event::ArrowLeft) return KeyChord{.Special = SpecialKey::Left};
    if (event == ftxui::Event::ArrowRight) return KeyChord{.Special = SpecialKey::Right};
    if (event == ftxui::Event::ArrowUpCtrl) return KeyChord{.Control = true, .Special = SpecialKey::Up};
    if (event == ftxui::Event::ArrowDownCtrl) return KeyChord{.Control = true, .Special = SpecialKey::Down};
    if (event == ftxui::Event::ArrowLeftCtrl) return KeyChord{.Control = true, .Special = SpecialKey::Left};
    if (event == ftxui::Event::ArrowRightCtrl) return KeyChord{.Control = true, .Special = SpecialKey::Right};
    if (event == ftxui::Event::Tab) return KeyChord{.Special = SpecialKey::Tab};
    if (event == ftxui::Event::TabReverse) return KeyChord{.Shift = true, .Special = SpecialKey::Tab};
    if (event == ftxui::Event::Return) return KeyChord{.Special = SpecialKey::Enter};
    if (event == ftxui::Event::Escape) return KeyChord{.Special = SpecialKey::Escape};
    if (event == ftxui::Event::Backspace) return KeyChord{.Special = SpecialKey::Backspace};
    if (event == ftxui::Event::Delete) return KeyChord{.Special = SpecialKey::Delete};
    if (event == ftxui::Event::Home) return KeyChord{.Special = SpecialKey::Home};
    if (event == ftxui::Event::End) return KeyChord{.Special = SpecialKey::End};
    if (event == ftxui::Event::PageUp) return KeyChord{.Special = SpecialKey::PageUp};
    if (event == ftxui::Event::PageDown) return KeyChord{.Special = SpecialKey::PageDown};
    if (event == ftxui::Event::F1) return KeyChord{.Special = SpecialKey::F1};
    if (event == ftxui::Event::F2) return KeyChord{.Special = SpecialKey::F2};
    if (event == ftxui::Event::F3) return KeyChord{.Special = SpecialKey::F3};
    if (event == ftxui::Event::F4) return KeyChord{.Special = SpecialKey::F4};
    if (event == ftxui::Event::F5) return KeyChord{.Special = SpecialKey::F5};
    if (event == ftxui::Event::F6) return KeyChord{.Special = SpecialKey::F6};
    if (event == ftxui::Event::F7) return KeyChord{.Special = SpecialKey::F7};
    if (event == ftxui::Event::F8) return KeyChord{.Special = SpecialKey::F8};
    if (event == ftxui::Event::F9) return KeyChord{.Special = SpecialKey::F9};
    if (event == ftxui::Event::F10) return KeyChord{.Special = SpecialKey::F10};
    if (event == ftxui::Event::F11) return KeyChord{.Special = SpecialKey::F11};
    if (event == ftxui::Event::F12) return KeyChord{.Special = SpecialKey::F12};

    const std::string_view input = event.input();

    // A leading Escape byte followed by more bytes is Meta/Alt+<key> -- see
    // this file's header comment for why this is reliably distinguishable
    // from a real, separate Escape keystroke followed later by an
    // unrelated key.
    if (!input.empty() && static_cast<std::uint8_t>(input[0]) == 0x1B && input.size() > 1) {
        std::optional<KeyChord> base = DecodeBaseKey(input.substr(1));
        if (base) {
            base->Meta = true;
        }
        return base;
    }

    return DecodeBaseKey(input);
}

} // namespace ned::ui
