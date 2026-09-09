#include "Paint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Compositing.h"

namespace ned::ui {

namespace {

    double Clamp01(double v) {
        return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    }

    // Cumulative span boundaries for a weighted stop list. Weight belongs to
    // the span *ending* at its stop, so the first entry is always 0 and the
    // last is always 1.
    template <typename StopT>
    std::vector<double> SpanOffsets(const std::vector<StopT>& stops) {
        std::vector<double> offsets(stops.size(), 0.0);
        double              total = 0.0;
        for (std::size_t i = 1; i < stops.size(); ++i) {
            total += stops[i].weight > 0.0F ? stops[i].weight : 0.0F;
            offsets[i] = total;
        }
        if (total <= 0.0) {
            // Every span was zero-weight: fall back to an even split rather
            // than dividing by zero and collapsing the whole gradient onto
            // its first stop.
            for (std::size_t i = 0; i < stops.size(); ++i) {
                offsets[i] = stops.size() > 1 ? static_cast<double>(i) / (stops.size() - 1) : 0.0;
            }
            return offsets;
        }
        for (double& offset : offsets) {
            offset /= total;
        }
        return offsets;
    }

    template <typename StopT>
    std::pair<std::size_t, double> LocateSpan(const std::vector<StopT>& stops, double t) {
        const std::vector<double> offsets = SpanOffsets(stops);
        const double              clamped = Clamp01(t);
        for (std::size_t i = 1; i < stops.size(); ++i) {
            if (clamped <= offsets[i] || i + 1 == stops.size()) {
                const double span  = offsets[i] - offsets[i - 1];
                const double local = span > 0.0 ? (clamped - offsets[i - 1]) / span : 1.0;
                return {i, Clamp01(local)};
            }
        }
        return {stops.size() - 1, 1.0};
    }

    // Deterministic per-cell hash: the same cell must produce the same jitter
    // on every frame, or noise becomes animation.
    std::uint32_t CellHash(int x, int y) {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 0x9E3779B9u;
        h ^= static_cast<std::uint32_t>(y) * 0x85EBCA6Bu;
        h ^= h >> 15;
        h *= 0xC2B2AE35u;
        h ^= h >> 13;
        return h;
    }

    int PositiveMod(int value, int modulus) {
        if (modulus <= 0) {
            return 0;
        }
        return ((value % modulus) + modulus) % modulus;
    }

    // A pattern's second phase occupies this many cells of each cycle,
    // derived from the two stops' relative weights (their duty cycle). At
    // least one cell, so a heavily lopsided ratio still draws something.
    int PhaseBCells(const Paint& paint, int period) {
        if (paint.stops.size() < 2 || period <= 1) {
            return period > 0 ? period / 2 : 0;
        }
        const double a     = paint.stops[0].weight > 0.0F ? paint.stops[0].weight : 1.0F;
        const double b     = paint.stops[1].weight > 0.0F ? paint.stops[1].weight : 1.0F;
        const int    cells = static_cast<int>(std::lround(period * (b / (a + b))));
        return cells < 1 ? 1 : (cells > period - 1 ? period - 1 : cells);
    }

    Color Transparent() {
        return Color::RGB(0x000000).WithAlpha(0);
    }

    Cell WashCell(Color colour) {
        Cell cell;
        cell.character        = "";
        cell.background_color = colour;
        return cell;
    }

    Cell FadeCell(double amount, Color toward) {
        Cell cell;
        cell.character = "";
        // A Fade is expressed as a translucent *foreground* with no glyph,
        // which is exactly Screen::Blend's "modulate what is already here"
        // path. amount is how much of the original colour survives.
        const double pull     = Clamp01(1.0 - amount);
        cell.foreground_color = toward.WithAlpha(static_cast<std::uint8_t>(std::lround(pull * 255.0)));
        return cell;
    }

} // namespace

Paint::Paint()                                  = default;
Paint::Paint(const Paint& other)                = default;
Paint::Paint(Paint&& other) noexcept            = default;
Paint& Paint::operator=(const Paint& other)     = default;
Paint& Paint::operator=(Paint&& other) noexcept = default;
Paint::~Paint()                                 = default;

Paint SolidPaint(Color colour) {
    Paint paint;
    paint.kind  = PaintKind::Solid;
    paint.stops = {ColorStop{colour, 1.0F}};
    return paint;
}

Paint GradientPaint(PaintAxis axis, std::vector<ColorStop> stops) {
    Paint paint;
    paint.kind  = stops.size() > 1 ? PaintKind::Gradient : PaintKind::Solid;
    paint.axis  = axis;
    paint.stops = std::move(stops);
    return paint;
}

Paint FadePaint(PaintAxis axis, std::vector<FadeStop> stops) {
    Paint paint;
    paint.kind  = PaintKind::Fade;
    paint.axis  = axis;
    paint.fades = std::move(stops);
    return paint;
}

Paint PatternPaint(PatternKind pattern, int period, std::vector<ColorStop> stops) {
    Paint paint;
    paint.kind    = PaintKind::Pattern;
    paint.pattern = pattern;
    paint.period  = period > 0 ? period : 1;
    paint.stops   = std::move(stops);
    // Patterns default to declining glyph cells: a checkerboard behind code
    // is a readability disaster, so a pattern textures a surface's empty
    // space unless a theme explicitly overrides this.
    paint.policy = AlphaPolicy::Dither;
    return paint;
}

Paint BlurPaint(int radius, Color tint) {
    Paint paint;
    paint.kind   = PaintKind::Blur;
    paint.period = radius > 0 ? radius : 1;
    paint.stops  = {ColorStop{tint, 1.0F}};
    return paint;
}

Paint StackPaint(std::vector<Paint> layers) {
    Paint paint;
    paint.kind   = PaintKind::Stack;
    paint.layers = std::move(layers);
    return paint;
}

double AxisParam(PaintAxis axis, double u, double v) {
    switch (axis) {
        case PaintAxis::X:
            return Clamp01(u);
        case PaintAxis::Y:
            return Clamp01(v);
        case PaintAxis::Diag:
            return Clamp01((Clamp01(u) + Clamp01(v)) * 0.5);
        case PaintAxis::Radial: {
            const double dx = (Clamp01(u) - 0.5) * 2.0;
            const double dy = (Clamp01(v) - 0.5) * 2.0;
            return Clamp01(std::sqrt(dx * dx + dy * dy) / std::sqrt(2.0));
        }
    }
    return 0.0;
}

Color GradientAt(const std::vector<ColorStop>& stops, double t) {
    if (stops.empty()) {
        return Transparent();
    }
    if (stops.size() == 1) {
        return stops.front().colour;
    }
    const auto [index, local] = LocateSpan(stops, t);
    const Color& from         = stops[index - 1].colour;
    const Color& to           = stops[index].colour;
    if (!from.Composable() || !to.Composable()) {
        // Nothing sensible to interpolate between (Default has no RGB), so
        // snap rather than invent a colour.
        return local < 0.5 ? from : to;
    }

    auto lerp = [local](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(std::lround(a + (b - a) * local));
    };
    return Color{.kind  = Color::Kind::TrueColor,
                 .red   = lerp(from.red, to.red),
                 .green = lerp(from.green, to.green),
                 .blue  = lerp(from.blue, to.blue),
                 .alpha = lerp(from.alpha, to.alpha)};
}

double FadeAt(const std::vector<FadeStop>& stops, double t) {
    if (stops.empty()) {
        return 1.0;
    }
    if (stops.size() == 1) {
        return Clamp01(stops.front().amount);
    }
    const auto [index, local] = LocateSpan(stops, t);
    const double from         = Clamp01(stops[index - 1].amount);
    const double to           = Clamp01(stops[index].amount);
    return from + (to - from) * local;
}

int PatternPhase(const Paint& paint, int x, int y) {
    const int period = paint.period > 0 ? paint.period : 1;
    switch (paint.pattern) {
        case PatternKind::Checker: {
            const int cellX = static_cast<int>(std::floor(static_cast<double>(x) / period));
            const int cellY = static_cast<int>(std::floor(static_cast<double>(y) / period));
            return PositiveMod(cellX + cellY, 2);
        }
        case PatternKind::Stripes:
            return PositiveMod(x + y, period) < PhaseBCells(paint, period) ? 1 : 0;
        case PatternKind::Scanlines: {
            const int rows = period > 1 ? period : 2;
            return PositiveMod(y, rows) < PhaseBCells(paint, rows) ? 1 : 0;
        }
        case PatternKind::Grid:
            return (PositiveMod(x, period) == 0 || PositiveMod(y, period) == 0) ? 1 : 0;
        case PatternKind::Hatch:
            return PositiveMod(x - y, period) == 0 ? 1 : 0;
        case PatternKind::Dots:
            return (PositiveMod(x, period) == 0 && PositiveMod(y, period) == 0) ? 1 : 0;
        case PatternKind::Noise:
            return 1; // noise has one phase; the jitter happens in PaintColourAt
    }
    return 0;
}

Color PaintColourAt(const Paint& paint, double u, double v, int x, int y) {
    switch (paint.kind) {
        case PaintKind::Solid:
            return paint.stops.empty() ? Transparent() : paint.stops.front().colour;

        case PaintKind::Gradient:
            return GradientAt(paint.stops, AxisParam(paint.axis, u, v));

        case PaintKind::Pattern: {
            if (paint.stops.empty()) {
                return Transparent();
            }
            if (paint.pattern == PatternKind::Noise) {
                // Jitter the base colour's alpha by +/- period percent --
                // texture on an otherwise dead flat fill.
                const Color base = paint.stops.front().colour;
                if (!base.Composable()) {
                    return base;
                }
                const double amplitude = paint.period / 100.0;
                const double jitter    = ((CellHash(x, y) % 2001) / 1000.0 - 1.0) * amplitude;
                const double alpha     = Clamp01(base.alpha / 255.0 + jitter);
                return base.WithAlpha(static_cast<std::uint8_t>(std::lround(alpha * 255.0)));
            }
            const int phase = PatternPhase(paint, x, y);
            if (phase == 0) {
                return paint.stops.front().colour;
            }
            return paint.stops.size() > 1 ? paint.stops[1].colour : Transparent();
        }

        case PaintKind::Fade:
        case PaintKind::Blur:
        case PaintKind::Stack:
            // These need the destination cell, so they exist only through
            // Fill. Reporting "nothing" here keeps a misuse silent rather
            // than wrong.
            return Transparent();
    }
    return Transparent();
}

bool PaintsColour(const Paint& paint) {
    switch (paint.kind) {
        case PaintKind::Solid:
        case PaintKind::Gradient:
        case PaintKind::Pattern:
            return !paint.stops.empty();
        case PaintKind::Stack:
            return std::any_of(paint.layers.begin(), paint.layers.end(),
                               [](const Paint& layer) { return PaintsColour(layer); });
        case PaintKind::Fade:
        case PaintKind::Blur:
        default:
            return false;
    }
}

namespace {

    void FillBlur(Canvas& canvas, Box box, const Paint& paint) {
        const int width  = box.x_max - box.x_min + 1;
        const int height = box.y_max - box.y_min + 1;
        if (width <= 0 || height <= 0) {
            return;
        }

        // Read the region first: blurring in place would smear each cell
        // into the ones after it, which is a different (and much worse)
        // effect than a box blur.
        std::vector<Color> source(static_cast<std::size_t>(width) * height);
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                source[static_cast<std::size_t>(row) * width + col] =
                    canvas[{.x = box.x_min + col, .y = box.y_min + row}].background_color;
            }
        }

        const int   radius = paint.period > 0 ? paint.period : 1;
        const Color tint   = paint.stops.empty() ? Transparent() : paint.stops.front().colour;

        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                int    samples = 0;
                double r = 0.0, g = 0.0, b = 0.0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int sx = col + dx;
                        const int sy = row + dy;
                        if (sx < 0 || sy < 0 || sx >= width || sy >= height) {
                            continue;
                        }
                        const Color& sample = source[static_cast<std::size_t>(sy) * width + sx];
                        if (!sample.Composable()) {
                            continue; // the terminal's own background has no RGB to average
                        }
                        r += sample.red;
                        g += sample.green;
                        b += sample.blue;
                        ++samples;
                    }
                }
                if (samples == 0) {
                    // Nothing known underneath: a blur has nothing to work
                    // with, so fall back to the tint alone.
                    if (tint.alpha > 0) {
                        canvas.Blend({.x = box.x_min + col, .y = box.y_min + row}, WashCell(tint), paint.policy);
                    }
                    continue;
                }
                Color blurred = Color::RGB(static_cast<std::uint8_t>(std::lround(r / samples)),
                                           static_cast<std::uint8_t>(std::lround(g / samples)),
                                           static_cast<std::uint8_t>(std::lround(b / samples)));
                canvas.Blend({.x = box.x_min + col, .y = box.y_min + row}, WashCell(blurred), AlphaPolicy::Opaque);
                if (tint.alpha > 0) {
                    canvas.Blend({.x = box.x_min + col, .y = box.y_min + row}, WashCell(tint), paint.policy);
                }
            }
        }
    }

} // namespace

void Fill(Canvas& canvas, Box localBox, const Paint& paint) {
    const int width  = localBox.x_max - localBox.x_min + 1;
    const int height = localBox.y_max - localBox.y_min + 1;
    if (width <= 0 || height <= 0) {
        return;
    }

    if (paint.kind == PaintKind::Stack) {
        for (const Paint& layer : paint.layers) {
            Fill(canvas, localBox, layer);
        }
        return;
    }
    if (paint.kind == PaintKind::Blur) {
        FillBlur(canvas, localBox, paint);
        return;
    }

    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const Point  at{.x = localBox.x_min + col, .y = localBox.y_min + row};
            const double u = width > 1 ? static_cast<double>(col) / (width - 1) : 0.0;
            const double v = height > 1 ? static_cast<double>(row) / (height - 1) : 0.0;

            if (paint.kind == PaintKind::Fade) {
                const double amount = FadeAt(paint.fades, AxisParam(paint.axis, u, v));
                if (amount >= 1.0) {
                    continue;
                }
                // Fading means pulling the existing colour toward the
                // background it sits on -- the theme's own "less of this"
                // rather than a colour of its own.
                canvas.Blend(at, FadeCell(amount, canvas[at].background_color), paint.policy);
                continue;
            }

            const Color colour = PaintColourAt(paint, u, v, at.x, at.y);
            if (colour.alpha == 0) {
                continue;
            }
            canvas.Blend(at, WashCell(colour), paint.policy);
        }
    }
}

void Fill(Canvas& canvas, const Paint& paint) {
    Fill(canvas, Box{.x_min = 0, .x_max = canvas.size().width - 1, .y_min = 0, .y_max = canvas.size().height - 1},
         paint);
}

} // namespace ned::ui
