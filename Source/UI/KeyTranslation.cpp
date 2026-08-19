#include "KeyTranslation.h"

#include <notcurses/notcurses.h>

namespace ned::ui {

namespace {

    using editor::KeyChord;
    using editor::SpecialKey;

    // Named synthesized keys -- arrows, navigation, function keys -- matched
    // directly against Notcurses' own NCKEY_* constants rather than
    // hand-decoding CSI/SS3 escape sequences ourselves, the same "let the
    // library's own input parser do this" approach the FTXUI-era translator
    // already used against FTXUI's pre-parsed Event constants. Modifiers
    // (Ctrl/Shift/Alt) are read once, uniformly, off ncinput::modifiers for
    // every one of these -- a real simplification over FTXUI, which had no
    // pre-built Shift+Arrow constants and needed a hand-built raw-CSI
    // comparison for that case specifically (see this file's old history);
    // Notcurses reports Shift+Arrow the same modifier-bit way it reports
    // everything else, no special case needed.
    std::optional<SpecialKey> SpecialKeyFor(std::uint32_t id) {
        switch (id) {
            case NCKEY_UP:
                return SpecialKey::Up;
            case NCKEY_DOWN:
                return SpecialKey::Down;
            case NCKEY_LEFT:
                return SpecialKey::Left;
            case NCKEY_RIGHT:
                return SpecialKey::Right;
            case NCKEY_TAB:
                return SpecialKey::Tab;
            case NCKEY_ENTER:
                return SpecialKey::Enter;
            case NCKEY_ESC:
                return SpecialKey::Escape;
            case NCKEY_BACKSPACE:
                return SpecialKey::Backspace;
            case NCKEY_DEL:
                return SpecialKey::Delete;
            case NCKEY_HOME:
                return SpecialKey::Home;
            case NCKEY_END:
                return SpecialKey::End;
            case NCKEY_PGUP:
                return SpecialKey::PageUp;
            case NCKEY_PGDOWN:
                return SpecialKey::PageDown;
            case NCKEY_F01:
                return SpecialKey::F1;
            case NCKEY_F02:
                return SpecialKey::F2;
            case NCKEY_F03:
                return SpecialKey::F3;
            case NCKEY_F04:
                return SpecialKey::F4;
            case NCKEY_F05:
                return SpecialKey::F5;
            case NCKEY_F06:
                return SpecialKey::F6;
            case NCKEY_F07:
                return SpecialKey::F7;
            case NCKEY_F08:
                return SpecialKey::F8;
            case NCKEY_F09:
                return SpecialKey::F9;
            case NCKEY_F10:
                return SpecialKey::F10;
            case NCKEY_F11:
                return SpecialKey::F11;
            case NCKEY_F12:
                return SpecialKey::F12;
            default:
                return std::nullopt;
        }
    }

    // Pure modifier-key-by-itself presses (NCKEY_LSHIFT, NCKEY_LCTRL, ...),
    // only ever reported at all under the Kitty keyboard protocol -- these
    // carry no KeyChord meaning of their own (a real modifier press is
    // always folded into the *next* real key's own ncinput::modifiers
    // instead), so they're filtered out here rather than falling through to
    // DecodeBaseKey and being misread as a literal codepoint.
    bool IsBareModifierKey(std::uint32_t id) {
        return id >= NCKEY_LSHIFT && id <= NCKEY_L5SHIFT;
    }

    // Decodes a "base" key (no Meta applied) from an ncinput whose id is a
    // plain Unicode codepoint (not one of the synthesized NCKEY_* values
    // SpecialKeyFor already handles) -- Ctrl is read straight off
    // ncinput::modifiers wherever a real terminal populates it, with a
    // defensive fallback (id in the raw C0 control-byte range, no modifier
    // bit set) for terminals that only ever send the bare control byte and
    // never set NCKEY_MOD_CTRL at all -- the legacy path every terminal the
    // FTXUI-era translator supported still uses. Ned's own KeyChord doesn't
    // track Shift separately for printable characters (a capital letter's
    // codepoint already encodes it), matching that translator's behavior.
    std::optional<KeyChord> DecodeBaseKey(const ncinput& input) {
        const std::uint32_t id = input.id;

        if (id == 8 || id == 0x7F) { // some terminals send raw BS/DEL for physical Backspace
            return KeyChord{.Special = SpecialKey::Backspace};
        }
        // Byte 0x1F (US, "Unit Separator") is what a real terminal actually
        // sends for Ctrl+_ -- and, since terminals don't distinguish Shift
        // on top of a control byte, for a physical Ctrl+/ press too. Real
        // Emacs' own undo binding is C-_ for exactly this reason.
        if (id == 0x1F) {
            return KeyChord{.Control = true, .Codepoint = U'_'};
        }
        if (ncinput_ctrl_p(&input) && id < 0x80) {
            // NOT simply "id is already the unmodified base character" --
            // confirmed by reading Notcurses' own load_ncinput (in.c), not
            // assumed: for a raw C0 control byte specifically, Notcurses
            // itself pre-normalizes id to the *uppercase* ASCII letter
            // (`ni->id = ni->id + 'A' - 1`) before this code ever sees it,
            // on top of setting NCKEY_MOD_CTRL -- so id here is 'A'-'Z' for
            // a plain Ctrl+letter press, not 'a'-'z'. Every keymap binding
            // in this codebase is parsed from lowercase kbd notation
            // ("C-x" -> Codepoint='x', ParseKeyChord/Key.cpp), so handing
            // the uppercase codepoint straight through silently failed to
            // match any Ctrl+letter binding at all -- every C-x/C-s/C-p/...
            // chord in the entire default keymap, a real, confirmed bug
            // (not hypothetical) caught via live testing, not headless
            // tests (which never exercised a real ncinput). Lowercased
            // here so this always matches what Editor/Key.cpp's own parser
            // produces; a non-letter Ctrl'd codepoint (rare -- Ctrl+digit
            // etc., where Notcurses' own uppercasing doesn't apply since
            // isupper/islower is false for it) passes through unchanged.
            const char32_t codepoint = (id >= 'A' && id <= 'Z') ? static_cast<char32_t>(id - 'A' + 'a') : static_cast<char32_t>(id);
            return KeyChord{.Control = true, .Codepoint = codepoint};
        }
        if (id >= 1 && id <= 26) {
            // Defensive fallback for terminals that only ever send the raw
            // C0 control byte with no modifier bit set at all.
            return KeyChord{.Control = true, .Codepoint = static_cast<char32_t>('a' + id - 1)};
        }

        if (id == 0 || nckey_synthesized_p(id)) {
            return std::nullopt; // an unrecognized synthesized event (resize, signal, eof, media key, ...)
        }
        return KeyChord{.Codepoint = static_cast<char32_t>(id)};
    }

} // namespace

std::optional<KeyChord> TranslateKey(const Event& event) {
    if (event.is_mouse()) {
        return std::nullopt;
    }

    const ncinput& input = event.raw();

    // Only fire on press/repeat -- a held key's repeat should behave like a
    // fresh press (matches every terminal's/FTXUI's own pre-Notcurses
    // behavior), but a release carries no KeyChord meaning of its own; only
    // reported at all under the Kitty keyboard protocol, so most terminals
    // never produce this case, but it's real input Notcurses can hand us.
    if (input.evtype == NCTYPE_RELEASE) {
        return std::nullopt;
    }

    if (IsBareModifierKey(input.id)) {
        return std::nullopt;
    }

    std::optional<KeyChord> result;
    if (const std::optional<SpecialKey> special = SpecialKeyFor(input.id)) {
        result = KeyChord{.Special = *special};
    }
    else {
        result = DecodeBaseKey(input);
    }

    if (result) {
        // Shift on a plain codepoint is already encoded in the codepoint
        // itself (e.g. 'A' vs 'a') -- only worth flagging separately when
        // paired with a Special key (Shift+Arrow etc.), matching the
        // FTXUI-era translator's own behavior.
        if (ncinput_shift_p(&input) && result->Special != SpecialKey::None) {
            result->Shift = true;
        }
        if (ncinput_alt_p(&input)) {
            result->Meta = true;
        }
        // Control is already folded in by DecodeBaseKey for a plain
        // codepoint; a Special key paired with Ctrl (e.g. Ctrl+Arrow) still
        // needs it applied here, since SpecialKeyFor itself doesn't consult
        // modifiers at all.
        if (ncinput_ctrl_p(&input) && result->Special != SpecialKey::None) {
            result->Control = true;
        }
    }
    return result;
}

} // namespace ned::ui
