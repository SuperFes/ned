#include "TestEvents.h"

namespace ned::ui::test {

namespace {
    Event FromInput(ncinput input) {
        return Event(input);
    }

    ncinput SpecialInput(std::uint32_t id) {
        ncinput input{};
        input.id     = id;
        input.evtype = NCTYPE_PRESS;
        return input;
    }
} // namespace

Event Character(char32_t codepoint) {
    ncinput input{};
    input.id     = codepoint;
    input.evtype = NCTYPE_PRESS;
    return FromInput(input);
}

Event Character(char ch) {
    return Character(static_cast<char32_t>(static_cast<unsigned char>(ch)));
}

Event Character(std::string_view utf8) {
    if (utf8.empty()) {
        return Character(char32_t{0});
    }
    const auto  b0 = static_cast<unsigned char>(utf8[0]);
    char32_t    codepoint;
    std::size_t length;
    if (b0 < 0x80) {
        codepoint = b0;
        length    = 1;
    }
    else if ((b0 & 0xE0) == 0xC0 && utf8.size() >= 2) {
        codepoint = b0 & 0x1F;
        length    = 2;
    }
    else if ((b0 & 0xF0) == 0xE0 && utf8.size() >= 3) {
        codepoint = b0 & 0x0F;
        length    = 3;
    }
    else if ((b0 & 0xF8) == 0xF0 && utf8.size() >= 4) {
        codepoint = b0 & 0x07;
        length    = 4;
    }
    else {
        return Character(char32_t{b0});
    }
    for (std::size_t i = 1; i < length; ++i) {
        codepoint = static_cast<char32_t>((codepoint << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F));
    }
    return Character(codepoint);
}

Event Ctrl(char letter) {
    ncinput input{};
    // Mirrors what Notcurses' own load_ncinput (in.c) does for a real raw
    // C0 control byte: uppercases the letter and sets NCKEY_MOD_CTRL -- see
    // KeyTranslation.cpp's own comment on DecodeBaseKey for why matching
    // this exact shape (not the more "obvious" lowercase-id-plus-modifier
    // shape) matters.
    input.id        = static_cast<std::uint32_t>(static_cast<unsigned char>(letter >= 'a' && letter <= 'z' ? letter - 'a' + 'A' : letter));
    input.modifiers = NCKEY_MOD_CTRL;
    input.evtype    = NCTYPE_PRESS;
    return FromInput(input);
}

Event Alt(char letter) {
    ncinput input{};
    input.id        = static_cast<std::uint32_t>(static_cast<unsigned char>(letter));
    input.modifiers = NCKEY_MOD_ALT;
    input.evtype    = NCTYPE_PRESS;
    return FromInput(input);
}

Event CtrlAlt(char letter) {
    ncinput input{};
    input.id        = static_cast<std::uint32_t>(static_cast<unsigned char>(letter >= 'a' && letter <= 'z' ? letter - 'a' + 'A' : letter));
    input.modifiers = NCKEY_MOD_CTRL | NCKEY_MOD_ALT;
    input.evtype    = NCTYPE_PRESS;
    return FromInput(input);
}

Event Return() {
    return FromInput(SpecialInput(NCKEY_ENTER));
}
Event Escape() {
    return FromInput(SpecialInput(NCKEY_ESC));
}
Event Tab() {
    return FromInput(SpecialInput(NCKEY_TAB));
}
Event TabReverse() {
    ncinput input = SpecialInput(NCKEY_TAB);
    input.modifiers |= NCKEY_MOD_SHIFT;
    return FromInput(input);
}
Event Backspace() {
    return FromInput(SpecialInput(NCKEY_BACKSPACE));
}
Event Delete() {
    return FromInput(SpecialInput(NCKEY_DEL));
}
Event Home() {
    return FromInput(SpecialInput(NCKEY_HOME));
}
Event End() {
    return FromInput(SpecialInput(NCKEY_END));
}
Event PageUp() {
    return FromInput(SpecialInput(NCKEY_PGUP));
}
Event PageDown() {
    return FromInput(SpecialInput(NCKEY_PGDOWN));
}
Event ArrowLeft() {
    return FromInput(SpecialInput(NCKEY_LEFT));
}
Event ArrowRight() {
    return FromInput(SpecialInput(NCKEY_RIGHT));
}
Event ArrowUp() {
    return FromInput(SpecialInput(NCKEY_UP));
}
Event ArrowDown() {
    return FromInput(SpecialInput(NCKEY_DOWN));
}
Event ArrowLeftCtrl() {
    ncinput input = SpecialInput(NCKEY_LEFT);
    input.modifiers |= NCKEY_MOD_CTRL;
    return FromInput(input);
}
Event ArrowRightCtrl() {
    ncinput input = SpecialInput(NCKEY_RIGHT);
    input.modifiers |= NCKEY_MOD_CTRL;
    return FromInput(input);
}
Event ArrowUpCtrl() {
    ncinput input = SpecialInput(NCKEY_UP);
    input.modifiers |= NCKEY_MOD_CTRL;
    return FromInput(input);
}
Event ArrowDownCtrl() {
    ncinput input = SpecialInput(NCKEY_DOWN);
    input.modifiers |= NCKEY_MOD_CTRL;
    return FromInput(input);
}
Event ArrowUpShift() {
    ncinput input = SpecialInput(NCKEY_UP);
    input.modifiers |= NCKEY_MOD_SHIFT;
    return FromInput(input);
}
Event ArrowDownShift() {
    ncinput input = SpecialInput(NCKEY_DOWN);
    input.modifiers |= NCKEY_MOD_SHIFT;
    return FromInput(input);
}
Event ArrowLeftShift() {
    ncinput input = SpecialInput(NCKEY_LEFT);
    input.modifiers |= NCKEY_MOD_SHIFT;
    return FromInput(input);
}
Event ArrowRightShift() {
    ncinput input = SpecialInput(NCKEY_RIGHT);
    input.modifiers |= NCKEY_MOD_SHIFT;
    return FromInput(input);
}

Event F(int n) {
    static constexpr std::uint32_t kFKeys[] = {NCKEY_F01, NCKEY_F02, NCKEY_F03, NCKEY_F04, NCKEY_F05, NCKEY_F06,
                                               NCKEY_F07, NCKEY_F08, NCKEY_F09, NCKEY_F10, NCKEY_F11, NCKEY_F12};
    return FromInput(SpecialInput(kFKeys[n - 1]));
}

Event Mouse(int x, int y, MouseEvent::Button button, MouseEvent::Motion motion, bool shift, bool meta, bool control) {
    ncinput input{};
    input.x = x;
    input.y = y;
    switch (button) {
        case MouseEvent::Button::Left:
            input.id = NCKEY_BUTTON1;
            break;
        case MouseEvent::Button::Middle:
            input.id = NCKEY_BUTTON2;
            break;
        case MouseEvent::Button::Right:
            input.id = NCKEY_BUTTON3;
            break;
        case MouseEvent::Button::WheelUp:
            input.id = NCKEY_BUTTON4;
            break;
        case MouseEvent::Button::WheelDown:
            input.id = NCKEY_BUTTON5;
            break;
        case MouseEvent::Button::None:
            input.id = NCKEY_MOTION;
            break;
    }
    switch (motion) {
        case MouseEvent::Motion::Pressed:
            input.evtype = NCTYPE_PRESS;
            break;
        case MouseEvent::Motion::Released:
            input.evtype = NCTYPE_RELEASE;
            break;
        case MouseEvent::Motion::Moved:
            input.evtype = NCTYPE_UNKNOWN;
            break;
    }
    if (shift)
        input.modifiers |= NCKEY_MOD_SHIFT;
    if (meta)
        input.modifiers |= NCKEY_MOD_ALT;
    if (control)
        input.modifiers |= NCKEY_MOD_CTRL;
    return FromInput(input);
}

} // namespace ned::ui::test
