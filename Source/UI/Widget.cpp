#include "Widget.h"

#include <notcurses/notcurses.h>

namespace ned::ui {

namespace {
    // Process-wide "who has keyboard focus" registry -- see Widget::TakeFocus
    // and FocusedWidget's own doc comments (Widget.h) for why a plain static
    // is the right shape here, mirroring TabWidth.h/ProjectRoot.h's own
    // mutex-guarded-static-state convention. Not actually mutex-guarded here:
    // unlike TabWidth/ProjectRoot (which can be written from a Janet call on
    // any thread), focus is only ever read/written from the main loop thread
    // that also drives every Widget's OnEvent/Paint -- the same "main-thread
    // only, no lock needed" assumption BufferView's own scratch-auto-save
    // background thread already respects by marshaling back via
    // ScreenInteractive::Post rather than touching widget state directly.
    Widget* g_focusedWidget = nullptr;
} // namespace

void Widget::TakeFocus() {
    g_focusedWidget = this;
}

Widget::~Widget() {
    if (g_focusedWidget == this) {
        g_focusedWidget = nullptr;
    }
}

Widget* FocusedWidget() {
    return g_focusedWidget;
}

namespace {
    // Standard-ish ANSI 16-color RGB approximations, in Palette16 index
    // order (0=Black ... 15=BrightWhite) -- xterm's own default palette
    // values, needed because there's no way to query a terminal's
    // actually-configured palette RGB values in-band. Only used by
    // Color::Interpolate below; Screen::Flush never needs this; it hands
    // Palette16 indices straight to Notcurses.
    constexpr std::uint8_t kPalette16Rgb[16][3] = {
        {0x00, 0x00, 0x00},
        {0x80, 0x00, 0x00},
        {0x00, 0x80, 0x00},
        {0x80, 0x80, 0x00},
        {0x00, 0x00, 0x80},
        {0x80, 0x00, 0x80},
        {0x00, 0x80, 0x80},
        {0xC0, 0xC0, 0xC0},
        {0x80, 0x80, 0x80},
        {0xFF, 0x00, 0x00},
        {0x00, 0xFF, 0x00},
        {0xFF, 0xFF, 0x00},
        {0x00, 0x00, 0xFF},
        {0xFF, 0x00, 0xFF},
        {0x00, 0xFF, 0xFF},
        {0xFF, 0xFF, 0xFF},
    };

} // namespace

void ColorToRgb8(const Color& color, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    switch (color.kind) {
        case Color::Kind::TrueColor:
            r = color.red;
            g = color.green;
            b = color.blue;
            return;
        case Color::Kind::Palette16:
            r = kPalette16Rgb[color.paletteIndex % 16][0];
            g = kPalette16Rgb[color.paletteIndex % 16][1];
            b = kPalette16Rgb[color.paletteIndex % 16][2];
            return;
        case Color::Kind::Default:
            r = g = b = 0x80; // neutral mid-gray -- Default has no real RGB value to blend from
            return;
    }
}

Color Color::Interpolate(float t, const Color& a, const Color& b) {
    // Equal endpoints come back unchanged, preserving a Default/Palette16
    // kind instead of degrading it to its RGB approximation -- the ANSI
    // fallback themes (Theme.h, ansi-fallback-theme follow-up) express "no
    // gradient" as gradientStart == gradientEnd, and on the terminals those
    // themes exist for (no truecolor at all) an approximated TrueColor
    // result would be exactly the wash-out the fallback is avoiding.
    if (a == b) {
        return a;
    }
    std::uint8_t ar, ag, ab, br, bg, bb;
    ColorToRgb8(a, ar, ag, ab);
    ColorToRgb8(b, br, bg, bb);
    t = std::clamp(t, 0.0F, 1.0F);
    return Color::RGB(static_cast<std::uint8_t>(ar + (static_cast<float>(br) - ar) * t),
                      static_cast<std::uint8_t>(ag + (static_cast<float>(bg) - ag) * t),
                      static_cast<std::uint8_t>(ab + (static_cast<float>(bb) - ab) * t));
}

bool Event::is_mouse() const {
    return nckey_mouse_p(input_.id);
}

MouseEvent Event::mouse() const {
    MouseEvent result;
    result.at = Point{input_.x, input_.y};

    switch (input_.id) {
        case NCKEY_BUTTON1:
            result.button = MouseEvent::Button::Left;
            break;
        case NCKEY_BUTTON2:
            result.button = MouseEvent::Button::Middle;
            break;
        case NCKEY_BUTTON3:
            result.button = MouseEvent::Button::Right;
            break;
        case NCKEY_BUTTON4:
            result.button = MouseEvent::Button::WheelUp;
            break;
        case NCKEY_BUTTON5:
            result.button = MouseEvent::Button::WheelDown;
            break;
        // horizontal-wheel-scroll follow-up: SGR mouse reporting's device
        // group 4-7 (see notcurses' own in.c mouse_click()) puts tilt-wheel
        // left/right here, buttons 6/7 -- xterm's own convention, the same
        // one every mainstream terminal emitting SGR mouse reports follows.
        case NCKEY_BUTTON6:
            result.button = MouseEvent::Button::WheelLeft;
            break;
        case NCKEY_BUTTON7:
            result.button = MouseEvent::Button::WheelRight;
            break;
        default:
            result.button = MouseEvent::Button::None;
            break; // includes NCKEY_MOTION and buttons 8-11 (unmapped)
    }

    switch (input_.evtype) {
        case NCTYPE_PRESS:
        case NCTYPE_REPEAT:
            result.motion = MouseEvent::Motion::Pressed;
            break;
        case NCTYPE_RELEASE:
            result.motion = MouseEvent::Motion::Released;
            break;
        default:
            result.motion = MouseEvent::Motion::Moved;
            break; // NCTYPE_UNKNOWN -- plain motion, no button transition
    }

    result.shift   = ncinput_shift_p(&input_);
    result.meta    = ncinput_alt_p(&input_);
    result.control = ncinput_ctrl_p(&input_);
    return result;
}

namespace {
    // Turns a Color into real Notcurses plane state -- the one place a
    // Color's kind/RGB bytes actually become ncplane_set_fg_*/set_bg_*
    // calls. No public accessor needed elsewhere: Screen::Flush is the only
    // caller.
    void ApplyForeground(ncplane* plane, const Color& color) {
        switch (color.kind) {
            case Color::Kind::Default:
                ncplane_set_fg_default(plane);
                break;
            case Color::Kind::Palette16:
                ncplane_set_fg_palindex(plane, color.paletteIndex);
                break;
            case Color::Kind::TrueColor:
                ncplane_set_fg_rgb8(plane, color.red, color.green, color.blue);
                break;
        }
    }

    void ApplyBackground(ncplane* plane, const Color& color) {
        switch (color.kind) {
            case Color::Kind::Default:
                ncplane_set_bg_default(plane);
                break;
            case Color::Kind::Palette16:
                ncplane_set_bg_palindex(plane, color.paletteIndex);
                break;
            case Color::Kind::TrueColor:
                ncplane_set_bg_rgb8(plane, color.red, color.green, color.blue);
                break;
        }
    }
} // namespace

void Screen::Flush(ncplane* plane) {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Cell& cell = cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];

            // `inverted` swaps which Color goes to which Notcurses channel
            // rather than relying on a style bit -- Notcurses does have
            // NCSTYLE_ITALIC/NCSTYLE_BOLD/NCSTYLE_UNDERLINE/NCSTYLE_STRUCK,
            // but no "reverse video" style bit, and a manual swap composes
            // correctly with true-color foregrounds/backgrounds where a
            // terminal-level SGR reverse code wouldn't.
            const Color& fg = cell.inverted ? cell.background_color : cell.foreground_color;
            const Color& bg = cell.inverted ? cell.foreground_color : cell.background_color;
            ApplyForeground(plane, fg);
            ApplyBackground(plane, bg);

            unsigned styles = NCSTYLE_NONE;
            if (cell.bold)
                styles |= NCSTYLE_BOLD;
            if (cell.italic)
                styles |= NCSTYLE_ITALIC;
            if (cell.underlined)
                styles |= NCSTYLE_UNDERLINE;
            if (cell.strikethrough)
                styles |= NCSTYLE_STRUCK;
            ncplane_set_styles(plane, styles);

            ncplane_putstr_yx(plane, y, x, cell.character.c_str());
        }
    }
}

} // namespace ned::ui
