//
// Paints and surfaces: what a theme says about a *region* rather than about
// a single color. This is the type layer behind Docs/Translucency.md's
// theming design -- the parser that turns `[:y "$bg" 3 "$bg+8"]` into these
// types lives with the theme engine (phase 4); everything here is already
// resolved, and is deliberately free of any parsing, theme lookup, or
// palette knowledge.
//
// The whole authoring grammar is one rule -- an optional axis or pattern
// keyword, then stop values, with numbers between them as relative weights
// -- so these structs are shaped to be exactly what that rule produces, and
// nothing more.
//

#ifndef NED_UI_PAINT_H
#define NED_UI_PAINT_H

#include <vector>

#include "Widget.h"

namespace ned::ui {

enum class PaintAxis {
    X,      // left to right
    Y,      // top to bottom (the default -- most chrome gradients run with the light)
    Diag,   // upper-left to lower-right
    Radial, // centre outward
};

enum class PatternKind {
    Checker,   // alternating period-sized squares
    Stripes,   // diagonal bands, one cycle every `period` cells
    Scanlines, // rows, one cycle every `period` rows (2 by default)
    Grid,      // single-cell rules every `period` -- graph paper
    Hatch,     // single-cell diagonal hatching every `period`
    Dots,      // stipple, one cell every `period` in both axes
    Noise,     // deterministic per-cell jitter of +/- `period` percent
};

enum class PaintKind {
    Solid,
    Gradient, // color stops along an axis
    Fade,     // percentage stops along an axis: modulates the color already there
    Pattern,
    Blur,  // sample what is beneath, box-blur it, optionally tint
    Stack, // several paints, applied in order
};

// `weight` is the number that appeared immediately before this stop in the
// source array. For a gradient that is the span *ending* at this stop, so
// the first stop's weight is unused; for a pattern it is this phase's share
// of the cycle, and the first stop's weight is used. Same field, same
// "relative share" meaning, and both readings fall out of the same parse.
struct ColorStop {
    Color colour;
    float weight = 1.0F;
};

struct FadeStop {
    float amount = 1.0F; // 0..1
    float weight = 1.0F;
};

struct Paint {
    PaintKind   kind    = PaintKind::Solid;
    PaintAxis   axis    = PaintAxis::Y;
    PatternKind pattern = PatternKind::Checker;

    // Pattern: cycle length in cells (noise: jitter amplitude, percent).
    // Blur: box radius in cells.
    int period = 1;

    std::vector<ColorStop> stops;  // Solid / Gradient / Pattern / Blur tint
    std::vector<FadeStop>  fades;  // Fade
    std::vector<Paint>     layers; // Stack, applied front to back

    // What to do when this paint lands on a cell whose background is the
    // terminal's own. Patterns default themselves to AlphaPolicy::Dither at
    // construction (see PatternPaint) so they never eat text.
    AlphaPolicy policy = AlphaPolicy::Auto;

    // Declared here and defaulted out-of-line (Paint.cpp) rather than left
    // implicit: `layers` is a vector of this very type, so at the point the
    // member is declared Paint is still incomplete and the implicit copy
    // assignment comes out deleted. Defining them where Paint is complete
    // gives back ordinary value semantics, which every registry and Surface
    // here relies on.
    Paint();
    Paint(const Paint& other);
    Paint(Paint&& other) noexcept;
    Paint& operator=(const Paint& other);
    Paint& operator=(Paint&& other) noexcept;
    ~Paint();
};

struct Shadow {
    int   dx     = 1;
    int   dy     = 1;
    int   radius = 1;
    Color colour = Color::RGB(0x000000).WithAlpha(90);
};

// A themed region. Popups, panels, tabs and the mode line are all this one
// type, which is what keeps them from each hand-rolling a look.
//
// No bold/italic/underlined/strikethrough here, and that is settled rather
// than pending: a Surface is *paints for a region*, and a paint interpolates
// -- that is what makes a gradient a gradient. A trait is binary per cell,
// with no meaningful "60% bold", so it would be a field that cannot take part
// in the one thing this type exists for.
//
// Traits stay a Brush concern. A widget taking its colours from a Surface
// takes its traits from the Brush beside it, via Brush::ApplyTextTo (which is
// ApplyTo minus the background the fill owns) -- TabBar is the clearest
// example. Every surface derived from a Brush inherits that Brush's trait
// keys for free, so `active_tab_bold` and friends already work; a surface
// with no Brush behind it (popup, panel, modeline) has no trait expression,
// which nothing has yet needed.
struct Surface {
    Paint  fill;
    Paint  border;
    Paint  text; // usually a Fade or a Solid; a Gradient here is for chrome only
    Shadow shadow;
    int    elevation = 0;
};

// --- construction conveniences (what a parser will emit) -----------------

[[nodiscard]] Paint SolidPaint(Color colour);
[[nodiscard]] Paint GradientPaint(PaintAxis axis, std::vector<ColorStop> stops);
[[nodiscard]] Paint FadePaint(PaintAxis axis, std::vector<FadeStop> stops);
[[nodiscard]] Paint PatternPaint(PatternKind pattern, int period, std::vector<ColorStop> stops);
[[nodiscard]] Paint BlurPaint(int radius, Color tint);
[[nodiscard]] Paint StackPaint(std::vector<Paint> layers);

// --- sampling ------------------------------------------------------------

// Where (u, v) -- both 0..1 within the painted box -- falls along an axis.
// Radial returns 0 at the centre and 1 at the corners.
[[nodiscard]] double AxisParam(PaintAxis axis, double u, double v);

// Weighted stop interpolation. An empty stop list is transparent; one stop
// is a solid.
[[nodiscard]] Color  GradientAt(const std::vector<ColorStop>& stops, double t);
[[nodiscard]] double FadeAt(const std::vector<FadeStop>& stops, double t);

// Which phase of a pattern a cell belongs to: 0 is the first stop, 1 the
// second. Absolute cell coordinates, so a pattern is anchored to the screen
// and does not crawl when a widget moves.
[[nodiscard]] int PatternPhase(const Paint& paint, int x, int y);

// The colour this paint contributes at one cell, or an empty (alpha 0)
// colour where it contributes nothing. Blur and Stack are not sampleable
// this way -- they need the destination, so they only exist through Fill.
[[nodiscard]] Color PaintColourAt(const Paint& paint, double u, double v, int x, int y);

// Whether this paint contributes a colour of its own (Solid/Gradient/
// Pattern) rather than modulating one that is already there (Fade) or
// nothing at all.
[[nodiscard]] bool PaintsColour(const Paint& paint);

// --- application ---------------------------------------------------------

// Paints across the whole canvas, or across one local-coordinate box of it.
// Gradient/Fade parameters are normalised against the painted box, so a
// gradient always spans exactly the region it was asked to fill.
void Fill(Canvas& canvas, const Paint& paint);
void Fill(Canvas& canvas, Box localBox, const Paint& paint);

} // namespace ned::ui

#endif // NED_UI_PAINT_H
