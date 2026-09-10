//
// Translucency phase 4b: every themed surface and every named paint, as a
// live swatch, with a contrast readout beside each.
//
// The point is the authoring loop (Docs/Translucency.md, "the feedback loop
// is the real feature"). A paint spec is a one-line string that resolves
// against slots of whatever theme is active -- there is no way to know what
// `[:diag "$accent" 2 "$keyword"]` looks like except to look at it, and no
// way to know whether the text over it stays readable except to measure it.
// This does both, over the *live* registries, so switching themes repaints
// it with no refresh step of its own.
//
// It is a Widget rather than a buffer for the reason the design doc's own
// "buffer showing every surface" phrasing cannot survive contact with the
// code: a buffer is text, and a swatch is a painted region. Nothing in
// text::Buffer can carry a Paint.
//
// Read-only and self-contained: no selection to activate, no model to push
// in from outside (it reads SurfaceNames()/NamedPaintNames() and the Theme
// reference it was constructed with, every Paint()). Scrolling and quit are
// the whole interaction -- TreeView's own Escape/C-g contract, plus the
// ordinary motion keys and the wheel.
//
// The contrast column is the "doubles as the contrast guard's reporting
// surface" half of the roadmap item. It reports WCAG contrast between the
// glyphs a surface actually paints and the background it actually paints
// them on -- read back out of the composited cells rather than recomputed
// from theme fields, so a dithered or translucent fill is measured as what
// it became, not as what it asked for.
//

#ifndef NED_UI_THEMEGALLERY_H
#define NED_UI_THEMEGALLERY_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class ThemeGallery : public Widget {
  public:
    // theme must outlive this widget (every themed widget here has the same
    // requirement) -- and is deliberately held by reference rather than
    // copied, since main.cpp's theme applier mutates that one object in
    // place and this panel is expected to follow a live theme switch.
    explicit ThemeGallery(const Theme& theme);

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

    // Escape or C-g while focused. Every other key is consumed (this panel
    // owns real keyboard focus, so leaking a keystroke to the buffer
    // underneath would edit it invisibly).
    void SetOnCancel(std::function<void()> onCancel);

    // Reset to the top. Called when the panel is shown, so re-opening it
    // never lands mid-list from a previous visit.
    void ScrollToTop();

    // One entry's worth of what the panel knows, exposed for tests (which
    // otherwise have to read composited cells back out of a Screen to
    // assert anything). Built fresh from the live registries.
    struct Entry {
        enum class Kind {
            Surface, // a SurfaceNames() entry -- fill, border and text
            Paint,   // a NamedPaintNames() entry -- one paint, no parts
        };
        Kind        kind = Kind::Surface;
        std::string name;
        // True when a theme actually set this, false when it is the default
        // derived from Theme's flat colour fields. Only meaningful for a
        // Surface: every named paint is by definition something someone
        // registered (bundled presets included).
        bool overridden = false;
    };

    [[nodiscard]] std::vector<Entry> Entries() const;

  private:
    // Rows per entry: two painted rows so a Y/diagonal/radial gradient has
    // somewhere to ramp, and one blank row between entries so adjacent
    // swatches do not read as one band.
    static constexpr int kSwatchHeight = 2;
    static constexpr int kEntryHeight  = kSwatchHeight + 1;

    // The left column carrying each entry's name and its contrast readout,
    // on the panel's own background rather than over the swatch -- a label
    // painted over the very gradient it names is the one thing a gallery
    // cannot afford to have become unreadable.
    static constexpr int kLabelWidth = 26;

    // WCAG's floor for text that is meant to be read at a glance but is not
    // body copy -- the same 3.0 Tests/ThemeTest.cpp holds every bundled
    // theme's chrome to, and chrome is what a surface mostly is.
    static constexpr double kContrastFloor = 3.0;

    // Scrolling is by whole entries, never by rows. A Canvas clips against
    // its own box rather than its parent's, so a band scrolled half off the
    // top would paint straight over the border and title; and a half-drawn
    // gradient misreports the very thing this panel exists to show. Both
    // problems disappear together if a partially-fitting entry is simply
    // not drawn.
    [[nodiscard]] int VisibleEntryCount(int interiorHeight) const;
    void              ClampScroll(int interiorHeight, std::size_t entryCount);

    // Paints one entry into the interior canvas, `row` entries down from
    // the first visible one. Returns the worst WCAG contrast ratio measured
    // between a glyph it wrote and the background that glyph actually
    // landed on, or std::nullopt when nothing measurable was written.
    std::optional<double> PaintEntry(Canvas& interior, const Entry& entry, int row, int interiorWidth);

    const Theme&          theme_;
    std::function<void()> onCancel_;
    std::size_t           scrollEntry_ = 0; // index of the first drawn entry
};

} // namespace ned::ui

#endif // NED_UI_THEMEGALLERY_H
