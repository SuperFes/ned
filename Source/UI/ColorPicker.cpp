#include "ColorPicker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

#include "Border.h"
#include "Compositing.h"
#include "KeyTranslation.h"
#include "Paint.h"
#include "ThemePaints.h"

namespace ned::ui {

namespace {

    bool IsQuit(const editor::KeyChord& chord) {
        return chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g');
    }

    // Black or white, whichever a reader can actually see on `background`.
    // Used for every glyph this panel writes on top of a swatch, where the
    // theme's own foreground is no guide at all -- the swatch is whatever
    // colour the user just dialled in.
    Color ReadableOn(const Color& background) {
        return ContrastRatio(Color::White, background) >= ContrastRatio(Color::Black, background) ? Color::White
                                                                                                  : Color::Black;
    }

    Color Solid(const editor::ColorValue& colour) {
        return Color::RGB(editor::ColorChannelToByte(colour.red), editor::ColorChannelToByte(colour.green),
                          editor::ColorChannelToByte(colour.blue));
    }

    std::string FormatRatio(double ratio) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%.1f", ratio);
        return buffer;
    }

    // The notation a colour takes when its alpha crosses 1.0 in either
    // direction. Without this, nudging the alpha row would silently reset a
    // chosen `hsl()` back to hex, because ColorPresentations offers an
    // entirely different candidate set once a colour stops being opaque.
    editor::ColorSyntax AlphaSibling(editor::ColorSyntax syntax) {
        switch (syntax) {
            case editor::ColorSyntax::Hex:
                return editor::ColorSyntax::HexAlpha;
            case editor::ColorSyntax::HexAlpha:
                return editor::ColorSyntax::Hex;
            case editor::ColorSyntax::HexShort:
                return editor::ColorSyntax::HexShortAlpha;
            case editor::ColorSyntax::HexShortAlpha:
                return editor::ColorSyntax::HexShort;
            case editor::ColorSyntax::Rgb:
                return editor::ColorSyntax::Rgba;
            case editor::ColorSyntax::Rgba:
                return editor::ColorSyntax::Rgb;
            case editor::ColorSyntax::Hsl:
                return editor::ColorSyntax::Hsla;
            case editor::ColorSyntax::Hsla:
                return editor::ColorSyntax::Hsl;
            case editor::ColorSyntax::Hwb:
            case editor::ColorSyntax::Named:
            case editor::ColorSyntax::Unknown:
                return syntax;
        }
        return syntax;
    }

    constexpr std::string_view kChannelLetters = "RGBHSLA";

    // "txt 2.5!" / "bg 21.0" / "bg -" -- the dash is a theme whose background
    // is the terminal's own with nothing detected behind it, where there is
    // genuinely nothing to measure against.
    std::string ContrastReadout(std::string_view label, std::optional<double> ratio, double floor) {
        if (!ratio) {
            return std::string(label) + " -";
        }
        return std::string(label) + " " + FormatRatio(*ratio) + (*ratio < floor ? "!" : "");
    }

    // The key hints, longest first: the panel is only as wide as the terminal
    // allows and a truncated hint row is worse than a terser complete one.
    // Column counts are spelled out because the arrows are multi-byte.
    struct FooterHint {
        int              columns;
        std::string_view text;
    };
    constexpr std::array<FooterHint, 3> kFooterHints{{
        {64, "↑↓ channel  ←→ adjust  Tab notation  r reset  RET set  Esc cancel"},
        {46, "↑↓ chan ←→ adj  Tab fmt  r reset  RET set  Esc"},
        {28, "↑↓←→  Tab  r  RET  Esc"},
    }};

    std::string_view FooterFor(int width) {
        for (const FooterHint& hint : kFooterHints) {
            if (hint.columns <= width) {
                return hint.text;
            }
        }
        return kFooterHints.back().text;
    }

} // namespace

ColorPicker::ColorPicker(const Theme& theme) : theme_(theme) {
}

void ColorPicker::SetOnAccept(std::function<void(std::string)> onAccept) {
    onAccept_ = std::move(onAccept);
}

void ColorPicker::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void ColorPicker::Open(const editor::ColorValue& initial, editor::ColorSyntax syntax,
                       const editor::ColorLiteralOptions& options) {
    options_  = options;
    original_ = initial;
    rgb_      = initial;
    hsl_      = editor::RgbToHsl(initial);
    selected_ = 0;
    notation_ = 0;

    presentations_ = editor::ColorPresentations(rgb_, options_);
    for (std::size_t index = 0; index < presentations_.size(); ++index) {
        if (presentations_[index].syntax == syntax) {
            notation_ = index;
            break;
        }
    }
}

std::string ColorPicker::Text() const {
    if (notation_ >= presentations_.size()) {
        return {};
    }
    return presentations_[notation_].text;
}

int ColorPicker::ChannelMax(Channel channel) {
    switch (channel) {
        case Channel::Red:
        case Channel::Green:
        case Channel::Blue:
            return 255;
        case Channel::Hue:
            return 359;
        case Channel::Saturation:
        case Channel::Lightness:
        case Channel::Alpha:
            return 100;
    }
    return 255;
}

int ColorPicker::ChannelValue(Channel channel) const {
    const auto scaled = [](double value, double span) {
        return static_cast<int>(std::lround(value * span));
    };
    switch (channel) {
        case Channel::Red:
            return scaled(rgb_.red, 255.0);
        case Channel::Green:
            return scaled(rgb_.green, 255.0);
        case Channel::Blue:
            return scaled(rgb_.blue, 255.0);
        case Channel::Hue:
            return static_cast<int>(std::lround(hsl_.hue)) % 360;
        case Channel::Saturation:
            return scaled(hsl_.saturation, 100.0);
        case Channel::Lightness:
            return scaled(hsl_.lightness, 100.0);
        case Channel::Alpha:
            return scaled(rgb_.alpha, 100.0);
    }
    return 0;
}

void ColorPicker::SetRgb(const editor::ColorValue& rgb) {
    rgb_ = rgb;

    // Hue survives a trip through grey; saturation does not. Reporting a
    // saturation the colour does not have would make the S row lie, but
    // dropping the hue would make the picker forget where it was the
    // moment a channel hit an extreme -- and there is no recovering it
    // from the colour, which is exactly what RgbToHsl's own contract says.
    const editor::HslColor derived = editor::RgbToHsl(rgb);
    hsl_.saturation                = derived.saturation;
    hsl_.lightness                 = derived.lightness;
    hsl_.alpha                     = rgb.alpha;
    if (derived.saturation > 0.0) {
        hsl_.hue = derived.hue;
    }
    RefreshPresentations();
}

void ColorPicker::SetHsl(const editor::HslColor& hsl) {
    hsl_ = hsl;
    rgb_ = editor::HslToRgb(hsl);
    RefreshPresentations();
}

void ColorPicker::RefreshPresentations() {
    const editor::ColorSyntax previous =
        notation_ < presentations_.size() ? presentations_[notation_].syntax : editor::ColorSyntax::Hex;

    presentations_ = editor::ColorPresentations(rgb_, options_);
    notation_      = 0;
    for (const editor::ColorSyntax wanted : {previous, AlphaSibling(previous)}) {
        const auto found = std::find_if(presentations_.begin(), presentations_.end(),
                                        [wanted](const editor::ColorPresentation& presentation) {
                                            return presentation.syntax == wanted;
                                        });
        if (found != presentations_.end()) {
            notation_ = static_cast<std::size_t>(std::distance(presentations_.begin(), found));
            return;
        }
    }
}

void ColorPicker::SetTo(int value) {
    const Channel channel = SelectedChannel();
    const int     max     = ChannelMax(channel);
    // Hue wraps; every other channel clamps. A colour wheel has no ends,
    // and stopping at 359 is the one place a slider's edge is wrong.
    const int bounded = channel == Channel::Hue ? ((value % 360) + 360) % 360 : std::clamp(value, 0, max);

    switch (channel) {
        case Channel::Red:
            SetRgb({.red = bounded / 255.0, .green = rgb_.green, .blue = rgb_.blue, .alpha = rgb_.alpha});
            return;
        case Channel::Green:
            SetRgb({.red = rgb_.red, .green = bounded / 255.0, .blue = rgb_.blue, .alpha = rgb_.alpha});
            return;
        case Channel::Blue:
            SetRgb({.red = rgb_.red, .green = rgb_.green, .blue = bounded / 255.0, .alpha = rgb_.alpha});
            return;
        case Channel::Hue:
            SetHsl({.hue        = static_cast<double>(bounded),
                    .saturation = hsl_.saturation,
                    .lightness  = hsl_.lightness,
                    .alpha      = hsl_.alpha});
            return;
        case Channel::Saturation:
            SetHsl({.hue        = hsl_.hue,
                    .saturation = bounded / 100.0,
                    .lightness  = hsl_.lightness,
                    .alpha      = hsl_.alpha});
            return;
        case Channel::Lightness:
            SetHsl({.hue        = hsl_.hue,
                    .saturation = hsl_.saturation,
                    .lightness  = bounded / 100.0,
                    .alpha      = hsl_.alpha});
            return;
        case Channel::Alpha: {
            editor::ColorValue next = rgb_;
            next.alpha              = bounded / 100.0;
            SetRgb(next);
            return;
        }
    }
}

void ColorPicker::Adjust(int delta) {
    SetTo(ChannelValue(SelectedChannel()) + delta);
}

editor::ColorValue ColorPicker::ChannelRamp(Channel channel, double fraction) const {
    editor::ColorValue ramp = rgb_;
    switch (channel) {
        case Channel::Red:
            ramp.red = fraction;
            return ramp;
        case Channel::Green:
            ramp.green = fraction;
            return ramp;
        case Channel::Blue:
            ramp.blue = fraction;
            return ramp;
        case Channel::Hue:
            return editor::HslToRgb({.hue        = fraction * 360.0,
                                     .saturation = hsl_.saturation,
                                     .lightness  = hsl_.lightness,
                                     .alpha      = rgb_.alpha});
        case Channel::Saturation:
            return editor::HslToRgb({.hue        = hsl_.hue,
                                     .saturation = fraction,
                                     .lightness  = hsl_.lightness,
                                     .alpha      = rgb_.alpha});
        case Channel::Lightness:
            return editor::HslToRgb(
                {.hue = hsl_.hue, .saturation = hsl_.saturation, .lightness = fraction, .alpha = rgb_.alpha});
        case Channel::Alpha:
            ramp.alpha = fraction;
            return ramp;
    }
    return ramp;
}

Color ColorPicker::Backdrop() const {
    return ChromeBackdrop(theme_);
}

Color ColorPicker::Composited(const editor::ColorValue& colour) const {
    const Color solid = Solid(colour);
    if (colour.alpha >= 1.0) {
        return solid;
    }
    return Color::Interpolate(static_cast<float>(std::clamp(colour.alpha, 0.0, 1.0)), Backdrop(), solid);
}

namespace {

    std::optional<double> MeasuredContrast(const Color& colour, const Color& against) {
        if (!colour.Composable() || !against.Composable()) {
            return std::nullopt;
        }
        return ContrastRatio(colour, against);
    }

} // namespace

std::optional<double> ColorPicker::ContrastAgainstText() const {
    return MeasuredContrast(Composited(rgb_), theme_.defaultForeground);
}

std::optional<double> ColorPicker::ContrastAgainstBackground() const {
    return MeasuredContrast(Composited(rgb_), Backdrop());
}

void ColorPicker::PaintPreview(Canvas& interior, int width) {
    const int   split     = std::max(1, width / 2);
    const Color wasColour = Composited(original_);
    const Color nowColour = Composited(rgb_);
    const int   labelRow  = kPreviewHeight / 2;

    for (int y = 0; y < kPreviewHeight; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell            = interior[{.x = x, .y = y}];
            cell.character        = " ";
            cell.background_color = x < split ? wasColour : nowColour;
            cell.foreground_color = ReadableOn(cell.background_color);
        }
    }

    const auto label = [&](int start, int end, std::string_view text, const Color& over) {
        const int span = end - start;
        if (span < static_cast<int>(text.size())) {
            return;
        }
        const Brush brush{.background = over, .foreground = ReadableOn(over)};
        PaintUtf8Row(interior, start + ((span - static_cast<int>(text.size())) / 2), labelRow, text, brush, span);
    };
    label(0, split, "was", wasColour);
    label(split, width, "now", nowColour);
}

void ColorPicker::PaintSlider(Canvas& interior, int row, int width, Channel channel) {
    const std::size_t index    = static_cast<std::size_t>(channel);
    const bool        selected = index == selected_;
    const int         value    = ChannelValue(channel);
    const int         max      = ChannelMax(channel);

    const Brush labelBrush{.background = theme_.background,
                           .foreground = selected ? theme_.defaultForeground : theme_.lineNumberForeground};
    interior[{.x = 0, .y = row}].character = selected ? "▸" : " ";
    labelBrush.ApplyTo(interior[{.x = 0, .y = row}]);
    interior[{.x = 1, .y = row}].character = std::string(1, kChannelLetters[index]);
    labelBrush.ApplyTo(interior[{.x = 1, .y = row}]);

    const int trackStart = kSliderLabelWidth;
    const int trackEnd   = width - kSliderValueWidth;
    const int trackWidth = trackEnd - trackStart;
    if (trackWidth > 0) {
        // The track is the channel's own ramp rather than a filled bar:
        // "what would this row's colour be at each position" is the
        // question a picker exists to answer, and a bar answers a
        // different one.
        const int marker = trackWidth == 1 ? 0
                                           : static_cast<int>(std::lround(static_cast<double>(value) / max *
                                                                          (trackWidth - 1)));
        for (int offset = 0; offset < trackWidth; ++offset) {
            const double fraction = trackWidth == 1 ? 0.0 : static_cast<double>(offset) / (trackWidth - 1);
            Cell&        cell     = interior[{.x = trackStart + offset, .y = row}];
            cell.background_color = Composited(ChannelRamp(channel, fraction));
            cell.foreground_color = ReadableOn(cell.background_color);
            cell.character        = offset == marker ? (selected ? "█" : "│") : " ";
        }
    }

    const std::string text    = std::to_string(value);
    const int         padding = std::max(0, kSliderValueWidth - 1 - static_cast<int>(text.size()));
    PaintUtf8Row(interior, trackEnd + 1 + padding, row, text, labelBrush, kSliderValueWidth);
}

void ColorPicker::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 2 || height <= 2) {
        return;
    }

    const Brush   interiorBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    const Surface surface              = SurfaceFor(theme_, "popup");
    const bool    fillReadsDestination = PaintReadsDestination(surface.fill);
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            Cell& cell     = c[{.x = x, .y = y}];
            cell.character = " ";
            interiorBrush.ApplyTextTo(cell);
            if (!fillReadsDestination) {
                cell.background_color = interiorBrush.background;
            }
        }
    }

    const int   interiorWidth  = width - 2;
    const int   interiorHeight = height - 2;
    const Point origin         = c.Origin();
    Canvas      interior       = c.ForBox(Box{.x_min = origin.x + 1,
                                              .x_max = origin.x + width - 2,
                                              .y_min = origin.y + 1,
                                              .y_max = origin.y + height - 2});

    Fill(interior, surface.fill);
    DrawBorder(c, theme_.border);
    RecolourBorder(c, surface.border);
    DrawBorderTitle(c, "Colour", theme_.borderAccent);

    if (interiorHeight >= kPreviewHeight) {
        PaintPreview(interior, interiorWidth);
    }

    const int firstSlider = kPreviewHeight + 1;
    for (std::size_t index = 0; index < kChannelCount; ++index) {
        const int row = firstSlider + static_cast<int>(index);
        if (row >= interiorHeight) {
            break;
        }
        PaintSlider(interior, row, interiorWidth, static_cast<Channel>(index));
    }

    const int valueRow = firstSlider + static_cast<int>(kChannelCount) + 1;
    if (valueRow < interiorHeight) {
        const Brush valueBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
        const int   end = PaintUtf8Row(interior, 1, valueRow, Text(), valueBrush, interiorWidth - 1);

        const std::string readout = ContrastReadout("txt", ContrastAgainstText(), kContrastFloor) + "  " +
                                    ContrastReadout("bg", ContrastAgainstBackground(), kContrastFloor);
        const int start = std::max(end + 2, interiorWidth - static_cast<int>(readout.size()));
        PaintUtf8Row(interior, start, valueRow, readout,
                     Brush{.background = theme_.background, .foreground = theme_.lineNumberForeground},
                     std::max(0, interiorWidth - start));
    }

    const int footerRow = interiorHeight - 1;
    if (footerRow > valueRow) {
        PaintUtf8Row(interior, 0, footerRow, FooterFor(interiorWidth),
                     Brush{.background = theme_.background, .foreground = theme_.lineNumberForeground},
                     interiorWidth);
    }
}

bool ColorPicker::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
        if (!mouse) {
            return false; // outside our own bounds -- not ours to consume
        }
        const int row = mouse->at.y - 1 - (kPreviewHeight + 1);
        if (row >= 0 && row < static_cast<int>(kChannelCount)) {
            selected_ = static_cast<std::size_t>(row);
            if (mouse->button == MouseEvent::Button::WheelUp) {
                Adjust(1);
            }
            else if (mouse->button == MouseEvent::Button::WheelDown) {
                Adjust(-1);
            }
            else if (mouse->button == MouseEvent::Button::Left && mouse->motion != MouseEvent::Motion::Released) {
                const int trackStart = 1 + kSliderLabelWidth;
                const int trackWidth = size().width - 1 - kSliderValueWidth - trackStart;
                if (trackWidth > 1 && mouse->at.x >= trackStart && mouse->at.x < trackStart + trackWidth) {
                    const double fraction = static_cast<double>(mouse->at.x - trackStart) / (trackWidth - 1);
                    SetTo(static_cast<int>(std::lround(fraction * ChannelMax(SelectedChannel()))));
                }
            }
        }
        return true;
    }
    if (!Focused()) {
        return false;
    }
    const auto chord = TranslateKey(event);
    if (!chord) {
        return true; // focused: swallow undecodable input rather than leaking it
    }
    if (IsQuit(*chord)) {
        if (onCancel_) {
            onCancel_();
        }
        return true;
    }

    const bool up    = chord->Special == editor::SpecialKey::Up || (chord->Control && chord->Codepoint == U'p');
    const bool down  = chord->Special == editor::SpecialKey::Down || (chord->Control && chord->Codepoint == U'n');
    const bool left  = chord->Special == editor::SpecialKey::Left || (chord->Control && chord->Codepoint == U'b');
    const bool right = chord->Special == editor::SpecialKey::Right || (chord->Control && chord->Codepoint == U'f');
    // A list this short wraps rather than clamping: there is no scroll
    // position to lose track of, and A is a step from R either way round.
    if (up) {
        selected_ = (selected_ + kChannelCount - 1) % kChannelCount;
    }
    else if (down) {
        selected_ = (selected_ + 1) % kChannelCount;
    }
    else if (left) {
        Adjust(chord->Shift ? -10 : -1);
    }
    else if (right) {
        Adjust(chord->Shift ? 10 : 1);
    }
    else if (chord->Special == editor::SpecialKey::PageUp) {
        Adjust(10);
    }
    else if (chord->Special == editor::SpecialKey::PageDown) {
        Adjust(-10);
    }
    else if (chord->Special == editor::SpecialKey::Home) {
        SetTo(0);
    }
    else if (chord->Special == editor::SpecialKey::End) {
        SetTo(ChannelMax(SelectedChannel()));
    }
    else if (chord->Special == editor::SpecialKey::Tab && !presentations_.empty()) {
        notation_ = chord->Shift ? (notation_ + presentations_.size() - 1) % presentations_.size()
                                 : (notation_ + 1) % presentations_.size();
    }
    else if (chord->Codepoint == U'r' && !chord->Control && !chord->Meta) {
        Open(original_, notation_ < presentations_.size() ? presentations_[notation_].syntax : editor::ColorSyntax::Hex,
             options_);
    }
    else if (chord->Special == editor::SpecialKey::Enter) {
        if (onAccept_) {
            onAccept_(Text());
        }
    }
    // Every other key is consumed: this panel owns real keyboard focus, so
    // anything leaking through would edit the buffer underneath invisibly.
    return true;
}

} // namespace ned::ui
