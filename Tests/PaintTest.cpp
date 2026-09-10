#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "UI/Compositing.h"
#include "UI/Paint.h"
#include "UI/Widget.h"

using ned::ui::AlphaPolicy;
using ned::ui::BlurPaint;
using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Color;
using ned::ui::ColorStop;
using ned::ui::ContrastRatio;
using ned::ui::EnsureContrast;
using ned::ui::FadeAt;
using ned::ui::FadePaint;
using ned::ui::FadeStop;
using ned::ui::Fill;
using ned::ui::GradientAt;
using ned::ui::GradientPaint;
using ned::ui::IsBlankGlyph;
using ned::ui::Paint;
using ned::ui::PaintAxis;
using ned::ui::PaintColourAt;
using ned::ui::PaintKind;
using ned::ui::PatternKind;
using ned::ui::PatternPaint;
using ned::ui::PatternPhase;
using ned::ui::Point;
using ned::ui::Screen;
using ned::ui::SolidPaint;
using ned::ui::StackPaint;
using ned::ui::Surface;

namespace {

Box WholeScreen(const Screen& screen) {
    return Box{.x_min = 0, .x_max = screen.Width() - 1, .y_min = 0, .y_max = screen.Height() - 1};
}

Canvas CanvasOver(Screen& screen) {
    return Canvas(screen, WholeScreen(screen));
}

void PaintOpaqueBackground(Screen& screen, Color colour) {
    for (int y = 0; y < screen.Height(); ++y) {
        for (int x = 0; x < screen.Width(); ++x) {
            screen.PixelAt(x, y).background_color = colour;
        }
    }
}

} // namespace

TEST_CASE("A weightless stop list splits evenly", "[Paint]") {
    const std::vector<ColorStop> stops = {
        ColorStop{Color::RGB(0x000000), 1.0F},
        ColorStop{Color::RGB(0xFFFFFF), 1.0F},
    };
    REQUIRE(GradientAt(stops, 0.0) == Color::RGB(0x000000));
    REQUIRE(GradientAt(stops, 1.0) == Color::RGB(0xFFFFFF));
    REQUIRE(static_cast<int>(GradientAt(stops, 0.5).red) == 128);
}

TEST_CASE("Weights are relative span sizes, and they are what offsets would have been", "[Paint]") {
    // "3" between the stops means the first span is three quarters of the
    // run -- the doc's own 3:1 example.
    const std::vector<ColorStop> stops = {
        ColorStop{Color::RGB(0x000000), 1.0F},
        ColorStop{Color::RGB(0xFFFFFF), 3.0F},
        ColorStop{Color::RGB(0x000000), 1.0F},
    };
    REQUIRE(GradientAt(stops, 0.75) == Color::RGB(0xFFFFFF));
    REQUIRE(static_cast<int>(GradientAt(stops, 0.375).red) == 128); // halfway along the first span

    SECTION("a stop at 35% is expressible exactly, as the doc claims") {
        const std::vector<ColorStop> asWeights = {
            ColorStop{Color::RGB(0x000000), 1.0F},
            ColorStop{Color::RGB(0xFFFFFF), 35.0F},
            ColorStop{Color::RGB(0x000000), 65.0F},
        };
        REQUIRE(GradientAt(asWeights, 0.35) == Color::RGB(0xFFFFFF));
    }
}

TEST_CASE("Gradients interpolate alpha as well as colour", "[Paint]") {
    const std::vector<ColorStop> stops = {
        ColorStop{Color::RGB(0x40C080).WithAlpha(255), 1.0F},
        ColorStop{Color::RGB(0x40C080).WithAlpha(0), 1.0F},
    };
    REQUIRE(static_cast<int>(GradientAt(stops, 0.5).alpha) == 128);
}

TEST_CASE("Degenerate stop lists do something sane", "[Paint]") {
    REQUIRE(GradientAt({}, 0.5).alpha == 0);
    REQUIRE(GradientAt({ColorStop{Color::RGB(0x123456), 1.0F}}, 0.9) == Color::RGB(0x123456));

    SECTION("all-zero weights fall back to an even split rather than dividing by zero") {
        const std::vector<ColorStop> stops = {
            ColorStop{Color::RGB(0x000000), 0.0F},
            ColorStop{Color::RGB(0xFFFFFF), 0.0F},
        };
        REQUIRE(static_cast<int>(GradientAt(stops, 0.5).red) == 128);
    }
}

TEST_CASE("Fade stops are percentages of what survives", "[Paint]") {
    const std::vector<FadeStop> stops = {FadeStop{1.0F, 1.0F}, FadeStop{0.0F, 1.0F}};
    REQUIRE(FadeAt(stops, 0.0) == 1.0);
    REQUIRE(FadeAt(stops, 1.0) == 0.0);
    REQUIRE(FadeAt(stops, 0.5) == 0.5);
    REQUIRE(FadeAt({}, 0.5) == 1.0); // no stops: nothing fades
}

TEST_CASE("PaintAxis parameters", "[Paint]") {
    REQUIRE(ned::ui::AxisParam(PaintAxis::X, 0.25, 0.75) == 0.25);
    REQUIRE(ned::ui::AxisParam(PaintAxis::Y, 0.25, 0.75) == 0.75);
    REQUIRE(ned::ui::AxisParam(PaintAxis::Diag, 0.0, 1.0) == 0.5);

    SECTION("radial is 0 at the centre and 1 in the corners") {
        REQUIRE(ned::ui::AxisParam(PaintAxis::Radial, 0.5, 0.5) == 0.0);
        REQUIRE(ned::ui::AxisParam(PaintAxis::Radial, 0.0, 0.0) > 0.99);
        REQUIRE(ned::ui::AxisParam(PaintAxis::Radial, 1.0, 1.0) > 0.99);
    }
}

TEST_CASE("Patterns are anchored to absolute cell position", "[Paint]") {
    const Paint checker = PatternPaint(PatternKind::Checker, 1,
                                       {ColorStop{Color::RGB(0x000000), 1.0F}, ColorStop{Color::RGB(0xFFFFFF), 1.0F}});

    REQUIRE(PatternPhase(checker, 0, 0) == 0);
    REQUIRE(PatternPhase(checker, 1, 0) == 1);
    REQUIRE(PatternPhase(checker, 0, 1) == 1);
    REQUIRE(PatternPhase(checker, 1, 1) == 0);

    SECTION("negative coordinates keep the same alternation") {
        REQUIRE(PatternPhase(checker, -1, 0) == 1);
        REQUIRE(PatternPhase(checker, -2, 0) == 0);
    }

    SECTION("period scales the squares") {
        const Paint big = PatternPaint(PatternKind::Checker, 2,
                                       {ColorStop{Color::RGB(0x000000), 1.0F}, ColorStop{Color::RGB(0xFFFFFF), 1.0F}});
        REQUIRE(PatternPhase(big, 0, 0) == 0);
        REQUIRE(PatternPhase(big, 1, 0) == 0);
        REQUIRE(PatternPhase(big, 2, 0) == 1);
    }
}

TEST_CASE("Pattern weights are a duty cycle", "[Paint]") {
    // The doc's own example: period 6, phases 1:3, so three quarters of each
    // cycle is the second stop.
    const Paint stripes = PatternPaint(PatternKind::Stripes, 6,
                                       {ColorStop{Color::RGB(0x000000), 1.0F}, ColorStop{Color::RGB(0xFFFFFF), 3.0F}});
    int         second  = 0;
    for (int x = 0; x < 6; ++x) {
        second += PatternPhase(stripes, x, 0);
    }
    REQUIRE(second == 5); // round(6 * 3/4)
}

TEST_CASE("Scanlines default to every other row", "[Paint]") {
    const Paint lines = PatternPaint(PatternKind::Scanlines, 2,
                                     {ColorStop{Color::RGB(0x000000), 1.0F}, ColorStop{Color::RGB(0xFFFFFF), 1.0F}});
    REQUIRE(PatternPhase(lines, 0, 0) == 1);
    REQUIRE(PatternPhase(lines, 0, 1) == 0);
    REQUIRE(PatternPhase(lines, 5, 2) == 1);
}

TEST_CASE("Noise is deterministic per cell", "[Paint]") {
    const Paint grain = PatternPaint(PatternKind::Noise, 8, {ColorStop{Color::RGB(0x202020).WithAlpha(128), 1.0F}});

    const Color first  = PaintColourAt(grain, 0.0, 0.0, 4, 7);
    const Color second = PaintColourAt(grain, 0.0, 0.0, 4, 7);
    REQUIRE(first == second); // static texture, never per-frame shimmer

    bool anyDifferent = false;
    for (int x = 0; x < 16 && !anyDifferent; ++x) {
        anyDifferent = PaintColourAt(grain, 0.0, 0.0, x, 0).alpha != first.alpha;
    }
    REQUIRE(anyDifferent); // and it is actually jittering something
}

TEST_CASE("Fill paints a gradient across exactly the box it was given", "[Paint]") {
    Screen screen(4, 1);
    PaintOpaqueBackground(screen, Color::RGB(0x000000));
    Canvas canvas = CanvasOver(screen);

    Fill(canvas, GradientPaint(PaintAxis::X,
                               {ColorStop{Color::RGB(0x000000), 1.0F}, ColorStop{Color::RGB(0xFFFFFF), 1.0F}}));

    REQUIRE(screen.PixelAt(0, 0).background_color == Color::RGB(0x000000));
    REQUIRE(screen.PixelAt(3, 0).background_color == Color::RGB(0xFFFFFF));
    REQUIRE(static_cast<int>(screen.PixelAt(1, 0).background_color.red) == 85);
}

TEST_CASE("Fill over a transparent background keeps the transparency", "[Paint]") {
    Screen screen(4, 2);
    Canvas canvas = CanvasOver(screen);

    Fill(canvas, SolidPaint(Color::RGB(0x40C080).WithAlpha(128)));

    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 2; ++y) {
            REQUIRE(screen.PixelAt(x, y).background_color == Color::Default);
            REQUIRE_FALSE(IsBlankGlyph(screen.PixelAt(x, y).character)); // dithered instead
        }
    }
}

TEST_CASE("A pattern declines cells that carry a glyph", "[Paint]") {
    Screen screen(2, 1);
    screen.PixelAt(0, 0).character        = "c";
    screen.PixelAt(0, 0).foreground_color = Color::RGB(0x00FF00);
    Canvas canvas                         = CanvasOver(screen);

    Fill(canvas, PatternPaint(PatternKind::Checker, 1,
                              {ColorStop{Color::RGB(0x000000).WithAlpha(0), 1.0F},
                               ColorStop{Color::RGB(0xFFFFFF).WithAlpha(128), 1.0F}}));

    REQUIRE(screen.PixelAt(0, 0).character == "c");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == Color::RGB(0x00FF00));
}

TEST_CASE("A Fade modulates the colour already there", "[Paint]") {
    Screen screen(2, 1);
    for (int x = 0; x < 2; ++x) {
        screen.PixelAt(x, 0).character        = "t";
        screen.PixelAt(x, 0).foreground_color = Color::RGB(0xFFFFFF);
        screen.PixelAt(x, 0).background_color = Color::RGB(0x000000);
    }
    Canvas canvas = CanvasOver(screen);

    Fill(canvas, FadePaint(PaintAxis::X, {FadeStop{1.0F, 1.0F}, FadeStop{0.0F, 1.0F}}));

    REQUIRE(screen.PixelAt(0, 0).character == "t");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == Color::RGB(0xFFFFFF)); // fully kept
    REQUIRE(screen.PixelAt(1, 0).foreground_color == Color::RGB(0x000000)); // fully gone
}

TEST_CASE("A Stack applies its layers in order", "[Paint]") {
    Screen screen(1, 1);
    PaintOpaqueBackground(screen, Color::RGB(0x000000));
    Canvas canvas = CanvasOver(screen);

    Fill(canvas, StackPaint({SolidPaint(Color::RGB(0xFFFFFF)), SolidPaint(Color::RGB(0x000000).WithAlpha(128))}));

    REQUIRE(static_cast<int>(screen.PixelAt(0, 0).background_color.red) == 127);
}

TEST_CASE("Blur averages what is beneath it", "[Paint]") {
    Screen screen(3, 1);
    screen.PixelAt(0, 0).background_color = Color::RGB(0x000000);
    screen.PixelAt(1, 0).background_color = Color::RGB(0xFFFFFF);
    screen.PixelAt(2, 0).background_color = Color::RGB(0x000000);
    Canvas canvas                         = CanvasOver(screen);

    Fill(canvas, BlurPaint(1, Color::RGB(0x000000).WithAlpha(0)));

    // Every cell now carries the average of its neighbourhood, and the
    // sharp white spike is gone.
    REQUIRE(static_cast<int>(screen.PixelAt(1, 0).background_color.red) == 85);
    REQUIRE(static_cast<int>(screen.PixelAt(0, 0).background_color.red) == 128);
}

TEST_CASE("Blur reads the whole region before writing any of it", "[Paint]") {
    // Blurring in place would smear each cell into the next; the middle cell
    // must see the *original* neighbours, not already-blurred ones.
    Screen screen(3, 1);
    screen.PixelAt(0, 0).background_color = Color::RGB(0x000000);
    screen.PixelAt(1, 0).background_color = Color::RGB(0x000000);
    screen.PixelAt(2, 0).background_color = Color::RGB(0x600000);
    Canvas canvas                         = CanvasOver(screen);

    Fill(canvas, BlurPaint(1, Color::RGB(0x000000).WithAlpha(0)));

    REQUIRE(static_cast<int>(screen.PixelAt(0, 0).background_color.red) == 0);
    REQUIRE(static_cast<int>(screen.PixelAt(1, 0).background_color.red) == 32);
}

TEST_CASE("Contrast ratio matches the WCAG endpoints", "[Paint]") {
    REQUIRE(ContrastRatio(Color::RGB(0x000000), Color::RGB(0xFFFFFF)) > 20.9);
    REQUIRE(ContrastRatio(Color::RGB(0x808080), Color::RGB(0x808080)) == 1.0);
    REQUIRE(ContrastRatio(Color::Default, Color::RGB(0x000000)) == 21.0); // nothing to judge
}

TEST_CASE("The contrast guard only moves what needs moving", "[Paint]") {
    const Color background = Color::RGB(0x101018);

    SECTION("a legible pair is returned untouched") {
        const Color fg = Color::RGB(0xE0E0E0);
        REQUIRE(EnsureContrast(fg, background, 4.5) == fg);
    }

    SECTION("a washed-out pair is pushed until it clears the floor") {
        const Color washed = Color::RGB(0x25252E); // barely visible on the background
        const Color fixed  = EnsureContrast(washed, background, 4.5);
        REQUIRE(ContrastRatio(fixed, background) >= 4.5);
    }

    SECTION("direction follows the background, so a light theme darkens instead") {
        const Color light  = Color::RGB(0xF0F0F0);
        const Color washed = Color::RGB(0xE4E4E4);
        const Color fixed  = EnsureContrast(washed, light, 4.5);
        REQUIRE(ned::ui::RelativeLuminance(fixed) < ned::ui::RelativeLuminance(washed));
    }
}

TEST_CASE("A Surface defaults to something inert", "[Paint]") {
    const Surface surface;
    REQUIRE(surface.fill.kind == PaintKind::Solid);
    REQUIRE(surface.fill.stops.empty()); // paints nothing until a theme says otherwise
    REQUIRE(surface.elevation == 0);
}
