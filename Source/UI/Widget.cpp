#include "Widget.h"

#include "Compositing.h"

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

// The five resolution rules from Docs/Translucency.md, in order. Which one
// applies is decided by what is *already* in the destination cell -- a
// background wash and a see-through background cannot coexist in one cell,
// and neither can a dither pattern and a glyph.
void Screen::Blend(int x, int y, const Cell& src, AlphaPolicy policy) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return;
    }
    Cell& dst = PixelAt(x, y);

    // Asymmetric on purpose. For the *source*, only a genuinely empty
    // character means "leave the destination's glyph alone" -- a space is a
    // glyph a caller meant to write, and treating it as nothing left blank
    // cells with no foreground of their own. For the *destination*, a space
    // is nothing: it is a cell a dither or a pattern may claim.
    const bool srcCarriesGlyph = !src.character.empty();
    const bool dstCarriesGlyph = !IsBlankGlyph(dst.character);

    // --- background ---------------------------------------------------
    if (src.background_color.Composable() && src.background_color.alpha > 0) {
        if (src.background_color.Opaque()) {
            // Rule 1: an opaque wash is just a write, so a fully opaque
            // Blend behaves exactly like the assignment it sits beside.
            dst.background_color = src.background_color;
        }
        else if (policy == AlphaPolicy::Opaque) {
            // Rule 5, checked before the others: this policy exists to
            // *stop* the transparency-preserving paths from running.
            dst.background_color = src.background_color.WithAlpha(255);
        }
        else if (dst.background_color.Composable()) {
            // Rule 2 (T1): a known destination color, so blend exactly.
            dst.background_color = BlendOver(dst.background_color, src.background_color);
        }
        else if (policy == AlphaPolicy::Skip) {
            // Nothing: this surface would rather show the terminal than
            // approximate itself.
        }
        else if (!dstCarriesGlyph && policy != AlphaPolicy::TintText) {
            // Rule 3 (T2): an empty cell over the terminal's own
            // background is the one place coverage dithering can run, and
            // it is what lets a wash reach the desktop behind a
            // translucent window.
            std::string glyph = DitherGlyph(src.background_color.alpha / 255.0, x, y);
            if (!glyph.empty()) {
                dst.character        = std::move(glyph);
                dst.foreground_color = src.background_color.WithAlpha(255);
                dst.bold             = false;
                dst.italic           = false;
                dst.underlined       = false;
                dst.strikethrough    = false;
                dst.inverted         = false;
            }
        }
        else if (policy != AlphaPolicy::Dither) {
            // Rule 4 (T3): the cell is carrying a glyph, so the only thing
            // left to move is its foreground. Keeps the text, keeps the
            // transparency. AlphaPolicy::Dither declines this on purpose --
            // a pattern tints the empty space of a surface, never its text.
            dst.foreground_color = TintToward(dst.foreground_color, src.background_color);
        }
    }

    // --- glyph and foreground ------------------------------------------
    if (srcCarriesGlyph) {
        dst.character     = src.character;
        dst.bold          = src.bold;
        dst.italic        = src.italic;
        dst.underlined    = src.underlined;
        dst.strikethrough = src.strikethrough;
        dst.inverted      = src.inverted;

        if (src.foreground_color.Opaque() || !src.foreground_color.Composable()) {
            dst.foreground_color = src.foreground_color;
        }
        else if (dst.background_color.Composable()) {
            // A translucent glyph color has the cell's own (already
            // resolved) background behind it, not the old foreground --
            // the glyph is being drawn *onto* that background.
            dst.foreground_color = BlendOver(dst.background_color, src.foreground_color);
        }
        else {
            dst.foreground_color = TintToward(dst.foreground_color, src.foreground_color);
        }
    }
    else if (src.foreground_color.Composable()) {
        // No glyph of its own, but a foreground color: this is a Fade,
        // whose job is to move the color already there rather than write a
        // glyph. Opaque is not a special case -- it is a fade that leaves
        // nothing of the original, which TintToward already resolves to the
        // wash color itself.
        dst.foreground_color = TintToward(dst.foreground_color, src.foreground_color);
    }
}

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

void Screen::Flush(ncplane* plane, ncplane* backingPlane) {

    // The backing layer first, so the text plane above has something to defer
    // to. Only its background is meaningful -- the glyph always comes from
    // the text plane.
    if (backingPlane != nullptr) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const Cell& cell = backing_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                                            static_cast<std::size_t>(x)];
                if (cell.background_color.kind == Color::Kind::Default) {
                    // Nothing here: stay out of the way entirely, so the
                    // terminal's own background still reaches a transparent
                    // theme's buffer.
                    ncplane_set_fg_alpha(backingPlane, NCALPHA_TRANSPARENT);
                    ncplane_set_bg_alpha(backingPlane, NCALPHA_TRANSPARENT);
                }
                else {
                    // Alpha is a *plane* attribute and persists across
                    // writes, so the opaque case has to say so explicitly --
                    // otherwise the first transparent cell leaves the plane
                    // transparent for every painted cell after it, and the
                    // whole layer silently draws nothing.
                    ncplane_set_bg_alpha(backingPlane, NCALPHA_OPAQUE);
                    ncplane_set_fg_alpha(backingPlane, NCALPHA_OPAQUE);
                    ApplyBackground(backingPlane, cell.background_color);
                    ncplane_set_fg_default(backingPlane);
                }
                ncplane_putstr_yx(backingPlane, y, x, " ");
            }
        }
    }

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
            if (backingPlane != nullptr && bg.kind == Color::Kind::Default) {
                // Defer rather than paint: NCALPHA_TRANSPARENT takes the
                // colour computed by lower planes, which is the backing
                // layer where it painted something and the terminal's own
                // background where it did not. ncplane_set_bg_default() would
                // instead paint the terminal default *over* the backing
                // layer, which is the whole difference between the two.
                ncplane_set_bg_alpha(plane, NCALPHA_TRANSPARENT);
            }
            else {
                ncplane_set_bg_alpha(plane, NCALPHA_OPAQUE); // see the backing loop: alpha persists per plane
                ApplyBackground(plane, bg);
            }

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
