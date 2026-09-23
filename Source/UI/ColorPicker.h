//
// The interactive half of the colour swatches: adjust a colour, see it, and
// write it back in the notation it was already spelled in.
//
// `color-at-point` converts between notations; it cannot change a colour.
// This can, and it is a Widget rather than a prompt for the reason
// ThemeGallery is: a colour is a painted region, and nothing in a
// ListPopup row or an echo-area prompt can show one ramping.
//
// Two representations, both authoritative, neither derived from the other on
// every keystroke:
//   - `Rgb()` is the value. It is what gets written out.
//   - `Hsl()` is the control state for the H/S/L rows.
// Editing one re-derives the other. The asymmetry is deliberate and is the
// whole reason the picker holds HSL at all: RgbToHsl is lossy at the
// achromatic extremes, so a colour driven to black through the L slider
// would come back with hue 0 and never return to where it started. Hue is
// therefore retained across any edit that lands on a grey, which is the
// behaviour every graphical picker has and the one a round trip through
// `ColorValue` alone cannot produce.
//
// The notation is part of the picker, not a separate menu: Tab cycles the
// spellings this colour can actually take (`ColorPresentations`, narrowed by
// the buffer's own `ColorLiteralOptions`), so the value row always shows the
// exact text an accept will write. That is also why `OnAccept` hands back a
// string rather than a `ColorValue` -- the widget already made the choice.
//
// The buffer is never edited while adjusting. One edit lands on accept, so
// the undo tree gets one entry rather than one per keystroke, and the
// swatch scan underneath is not re-run on every arrow key.
//

#ifndef NED_UI_COLORPICKER_H
#define NED_UI_COLORPICKER_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Editor/ColorLiteral.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class ColorPicker : public Widget {
  public:
    // theme must outlive this widget, and is held by reference so a live
    // theme switch repaints the picker -- the same contract ThemeGallery
    // has, and for the same reason (main.cpp mutates one Theme in place).
    explicit ColorPicker(const Theme& theme);

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

    // Resets to `initial` and selects `syntax` as the output notation when
    // this colour can still be written that way. Called every time the
    // picker is shown, so re-opening never inherits the last visit's colour.
    void Open(const editor::ColorValue& initial, editor::ColorSyntax syntax,
              const editor::ColorLiteralOptions& options);

    // The literal text an accept would write right now, or empty when the
    // colour can be spelled no way at all (unreachable in practice -- hex
    // always works -- but the paint path must not assume it).
    [[nodiscard]] std::string Text() const;

    [[nodiscard]] const editor::ColorValue& Rgb() const {
        return rgb_;
    }
    [[nodiscard]] const editor::HslColor& Hsl() const {
        return hsl_;
    }

    // Enter. The argument is Text(); the picker is not hidden by this --
    // the host does that, in the order that gives focus back before the
    // edit lands.
    void SetOnAccept(std::function<void(std::string)> onAccept);

    // Escape or C-g while focused. Every other key is consumed: this panel
    // owns real keyboard focus, so a leaked keystroke would edit the buffer
    // underneath invisibly.
    void SetOnCancel(std::function<void()> onCancel);

    // The adjustable rows, in paint order. Public for tests, which would
    // otherwise have to read composited cells back out of a Screen to
    // assert which row is selected.
    enum class Channel : std::uint8_t {
        Red,
        Green,
        Blue,
        Hue,
        Saturation,
        Lightness,
        Alpha,
    };
    static constexpr std::size_t kChannelCount = 7;

    [[nodiscard]] Channel SelectedChannel() const {
        return static_cast<Channel>(selected_);
    }

    // The channel's current value in the units its row displays: 0-255 for
    // R/G/B, 0-359 for H, 0-100 for S/L/A.
    [[nodiscard]] int ChannelValue(Channel channel) const;

    // WCAG contrast between the current colour and the theme's text
    // colour, and between it and the theme's background -- "would text over
    // this stay readable" and "would this stay readable as text". Both are
    // measured against the colour as it composites at its own alpha, since
    // that is what a reader would actually see.
    //
    // nullopt when the other side has no RGB value to measure: a theme whose
    // background is the terminal's own, with nothing detected behind it, is
    // a colour nobody here knows. Reporting that honestly matters more than
    // usual for this panel -- ContrastRatio answers 21.0 (its maximum) for an
    // unmeasurable pair, which would read as a perfect score rather than as
    // no score at all.
    [[nodiscard]] std::optional<double> ContrastAgainstText() const;
    [[nodiscard]] std::optional<double> ContrastAgainstBackground() const;

  private:
    // Rows of preview above the sliders. Two would do; three gives the
    // before/after split a middle row to carry its labels on without the
    // labels being the only thing either side shows.
    static constexpr int kPreviewHeight = 3;

    // Columns reserved left of each slider track (selection caret, channel
    // letter, space) and right of it (the numeric readout).
    static constexpr int kSliderLabelWidth = 4;
    static constexpr int kSliderValueWidth = 5;

    // WCAG's floor for body text. The readout says which side of it each
    // ratio falls on rather than refusing anything -- a colour literal in a
    // stylesheet is frequently not text at all, so this is information, not
    // a rule.
    static constexpr double kContrastFloor = 4.5;

    void Adjust(int delta);
    void SetTo(int value);
    void SetRgb(const editor::ColorValue& rgb);
    void SetHsl(const editor::HslColor& hsl);
    void RefreshPresentations();

    // The channel's inclusive upper bound in its own display units.
    [[nodiscard]] static int ChannelMax(Channel channel);

    // The colour this channel's track shows at `fraction` of its span --
    // the ramp the slider is painted as, which is the whole reason the
    // track is a gradient rather than a filled bar.
    [[nodiscard]] editor::ColorValue ChannelRamp(Channel channel, double fraction) const;

    // `colour` composited over the panel's own background at its alpha.
    // Every swatch and every contrast measurement goes through this: a
    // half-transparent colour that is measured as if it were opaque reports
    // a contrast nobody will ever see.
    [[nodiscard]] Color Composited(const editor::ColorValue& colour) const;

    // What the panel is actually painted on -- the theme's background, or
    // the detected backdrop when that is the terminal's own. Both the
    // compositing above and the readout below have to agree on it, or a
    // translucent swatch would be blended against one colour and measured
    // against another.
    [[nodiscard]] Color Backdrop() const;

    void PaintPreview(Canvas& interior, int width);
    void PaintSlider(Canvas& interior, int row, int width, Channel channel);

    const Theme& theme_;

    editor::ColorValue          original_;
    editor::ColorValue          rgb_;
    editor::HslColor            hsl_;
    editor::ColorLiteralOptions options_;

    std::vector<editor::ColorPresentation> presentations_;
    std::size_t                            notation_ = 0;
    std::size_t                            selected_ = 0;

    std::function<void(std::string)> onAccept_;
    std::function<void()>            onCancel_;
};

} // namespace ned::ui

#endif // NED_UI_COLORPICKER_H
