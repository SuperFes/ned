#include "KeyTranslation.h"

#include <array>

#include <notcurses/notcurses.h>

namespace ned::ui {

namespace {

    using editor::KeyChord;
    using editor::SpecialKey;

    // Named synthesized keys -- arrows, navigation, function keys -- matched
    // directly against Notcurses' own NCKEY_* constants rather than
    // hand-decoding CSI/SS3 escape sequences ourselves. Modifiers
    // (Ctrl/Shift/Alt) are read once, uniformly, off ncinput::modifiers for
    // every one of these, including Shift+Arrow -- Notcurses reports it the
    // same modifier-bit way it reports everything else, no special case
    // needed.
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

    // xterm/terminfo's own encoding for a MODIFIED function key. A terminal
    // without the kitty keyboard protocol has no way to say "Shift+F9", so
    // it sends a different function key entirely and sets no modifier bit
    // at all -- kf13..kf24 ARE shifted F1..F12 by terminfo convention, and
    // xterm extends the same idea through Ctrl, Ctrl+Shift and Alt.
    // Measured with this codebase's own key probe rather than assumed:
    // Shift+F9 arrives as F21, Ctrl+F9 as F33, Ctrl+Shift+F9 as F45 and
    // Alt+F9 as F57, every one of them with `modifiers` == 0.
    //
    // Folded back onto F1..F12 plus the modifiers they imply, so one keymap
    // entry written "S-F9" matches under both protocols -- a kitty-protocol
    // terminal reports F9 with the shift bit set and lands on the same
    // chord through the ordinary path below. Without this every S-F<n>
    // binding was silently dead: SpecialKeyFor stopped at F12 and returned
    // nullopt, so the keystroke was dropped before any keymap saw it.
    //
    // The cost is that a physical F13..F24 key (rare, and exactly what
    // terminfo already conflates these with) cannot be bound separately.
    std::optional<KeyChord> DecodeExtendedFunctionKey(std::uint32_t id) {
        if (id < NCKEY_F13 || id > NCKEY_F60) {
            return std::nullopt;
        }
        static constexpr std::array<SpecialKey, 12> kFunctionKeys{
            SpecialKey::F1, SpecialKey::F2, SpecialKey::F3, SpecialKey::F4, SpecialKey::F5, SpecialKey::F6,
            SpecialKey::F7, SpecialKey::F8, SpecialKey::F9, SpecialKey::F10, SpecialKey::F11, SpecialKey::F12};
        const std::uint32_t offset = id - NCKEY_F13;
        KeyChord            chord{.Special = kFunctionKeys[offset % kFunctionKeys.size()]};
        switch (offset / kFunctionKeys.size()) {
            case 0: // F13..F24
                chord.Shift = true;
                break;
            case 1: // F25..F36
                chord.Control = true;
                break;
            case 2: // F37..F48
                chord.Control = true;
                chord.Shift   = true;
                break;
            default: // F49..F60
                chord.Meta = true;
                break;
        }
        return chord;
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
    // never set NCKEY_MOD_CTRL at all. Ned's own KeyChord doesn't track
    // Shift separately for printable characters -- a capital letter's
    // codepoint already encodes it.
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
    // fresh press, but a release carries no KeyChord meaning of its own; only
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
    else if (std::optional<KeyChord> extended = DecodeExtendedFunctionKey(input.id)) {
        // Already carries the modifiers its own id implies; the bit-reading
        // below can only add to them, never contradict them (a terminal
        // using this encoding sets no bits at all).
        result = *extended;
    }
    else {
        result = DecodeBaseKey(input);
    }

    if (result) {
        // Shift on a plain codepoint is already encoded in the codepoint
        // itself (e.g. 'A' vs 'a') -- only worth flagging separately when
        // paired with a Special key (Shift+Arrow etc.).
        if (ncinput_shift_p(&input) && result->Special != SpecialKey::None) {
            result->Shift = true;
        }
        // BOTH the modifiers-bit accessor AND the deprecated legacy `alt`
        // bool -- a real, confirmed-via-live-probe Notcurses v3.0.14 bug,
        // not belt-and-suspenders: for a legacy-terminal Alt+letter press
        // (a fast ESC-prefixed letter -- every terminal without the kitty
        // keyboard protocol, including tmux), walk_automaton (automaton.c)
        // merges the two bytes into one ncinput but records Alt only in the
        // deprecated `ni->alt` bool, and load_ncinput (in.c) never syncs
        // the legacy bools into `modifiers` -- so ncinput_alt_p(), which
        // reads only `modifiers`, is false for the exact event shape M-x
        // arrives as in practice. Kitty-protocol terminals set `modifiers`
        // directly and every other path zero-initializes the struct, so
        // OR-ing the legacy field in can never false-positive. Found
        // because M-x didn't fire on real fast Alt+x presses while every
        // headless test (which constructed the kitty-style shape) passed.
        if (ncinput_alt_p(&input) || input.alt) {
            result->Meta = true;
        }
        // Control is already folded in by DecodeBaseKey for a plain
        // codepoint; a Special key paired with Ctrl (e.g. Ctrl+Arrow) still
        // needs it applied here, since SpecialKeyFor itself doesn't consult
        // modifiers at all.
        // copilot-key follow-up: unlike Shift, Super is NOT folded into the
        // codepoint by the terminal -- Super+x still arrives as a plain 'x'
        // -- so this applies to a literal character as well as to a special
        // key, which is why it isn't guarded on Special != None the way
        // Shift and Control are.
        if (ncinput_super_p(&input)) {
            result->Super = true;
        }
        if (ncinput_ctrl_p(&input) && result->Special != SpecialKey::None) {
            result->Control = true;
        }
    }
    return result;
}

} // namespace ned::ui
