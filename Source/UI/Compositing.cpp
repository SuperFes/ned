#include "Compositing.h"

#include <cmath>

namespace ned::ui {

namespace {

    std::uint8_t LerpByte(std::uint8_t from, std::uint8_t to, double t) {
        const double v = from + (to - from) * t;
        if (v <= 0.0) {
            return 0;
        }
        if (v >= 255.0) {
            return 255;
        }
        return static_cast<std::uint8_t>(v + 0.5);
    }

    // Braille dot bits are fixed by Unicode: column 0 top-to-bottom is
    // 0x01/0x02/0x04/0x40, column 1 is 0x08/0x10/0x20/0x80.
    constexpr std::uint8_t kBrailleDots[4][2] = {
        {0x01, 0x08},
        {0x02, 0x10},
        {0x04, 0x20},
        {0x40, 0x80},
    };

    constexpr int kBayer8[8][8] = {
        {0, 32, 8, 40, 2, 34, 10, 42},
        {48, 16, 56, 24, 50, 18, 58, 26},
        {12, 44, 4, 36, 14, 46, 6, 38},
        {60, 28, 52, 20, 62, 30, 54, 22},
        {3, 35, 11, 43, 1, 33, 9, 41},
        {51, 19, 59, 27, 49, 17, 57, 25},
        {15, 47, 7, 39, 13, 45, 5, 37},
        {63, 31, 55, 23, 61, 29, 53, 21},
    };

    // Positive modulo -- a cell at a negative coordinate must still index
    // the matrix, and C++'s % would hand back a negative index.
    int Wrap8(int v) {
        return ((v % 8) + 8) % 8;
    }

    std::string BrailleGlyph(int bits) {
        const unsigned cp = 0x2800u + static_cast<unsigned>(bits & 0xFF);
        std::string    out;
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        return out;
    }

} // namespace

Color BlendOver(const Color& dst, const Color& src) {
    if (src.alpha == 0) {
        return dst.WithAlpha(255);
    }
    if (src.alpha == 255 || !dst.Composable() || !src.Composable()) {
        return src.WithAlpha(255);
    }

    const double t = src.alpha / 255.0;
    return Color{.kind  = Color::Kind::TrueColor,
                 .red   = LerpByte(dst.red, src.red, t),
                 .green = LerpByte(dst.green, src.green, t),
                 .blue  = LerpByte(dst.blue, src.blue, t),
                 .alpha = 255};
}

Color TintToward(const Color& dst, const Color& wash) {
    if (!wash.Composable() || wash.alpha == 0) {
        return dst;
    }
    if (wash.Opaque()) {
        // Nothing of the original survives, which does not require knowing
        // what the original was -- so this works even where the destination
        // is the terminal's own colour and has no RGB to blend from.
        return wash;
    }
    if (!dst.Composable()) {
        return dst;
    }
    return BlendOver(dst, wash);
}

std::string DitherGlyph(double coverage, int x, int y) {
    if (coverage <= 0.0) {
        return {};
    }
    if (coverage >= 1.0) {
        return "█";
    }

    int bits = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 2; ++c) {
            const int    subY   = y * 4 + r;
            const int    subX   = x * 2 + c;
            const double thresh = (kBayer8[Wrap8(subY)][Wrap8(subX)] + 0.5) / 64.0;
            if (coverage > thresh) {
                bits |= kBrailleDots[r][c];
            }
        }
    }
    if (bits == 0) {
        return {};
    }
    if (bits == 0xFF) {
        return "█";
    }
    return BrailleGlyph(bits);
}

namespace {

    double LinearChannel(std::uint8_t value) {
        const double c = value / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }

} // namespace

double RelativeLuminance(const Color& colour) {
    return 0.2126 * LinearChannel(colour.red) + 0.7152 * LinearChannel(colour.green) +
           0.0722 * LinearChannel(colour.blue);
}

double ContrastRatio(const Color& a, const Color& b) {
    if (!a.Composable() || !b.Composable()) {
        return 21.0;
    }
    const double la      = RelativeLuminance(a);
    const double lb      = RelativeLuminance(b);
    const double lighter = la > lb ? la : lb;
    const double darker  = la > lb ? lb : la;
    return (lighter + 0.05) / (darker + 0.05);
}

Color EnsureContrast(const Color& foreground, const Color& background, double minRatio) {
    if (!foreground.Composable() || !background.Composable()) {
        return foreground;
    }
    if (ContrastRatio(foreground, background) >= minRatio) {
        return foreground;
    }

    // Move away from the background, not toward some fixed colour: on a dark
    // background the fix is lighter text, on a light one it is darker, and
    // choosing by the background's own luminance is what keeps a theme's hue
    // recognisable instead of driving everything to black or white.
    const Color target = RelativeLuminance(background) > 0.5 ? Color::RGB(0x000000) : Color::RGB(0xFFFFFF);

    Color best = foreground;
    for (int step = 1; step <= 20; ++step) {
        const Color candidate = BlendOver(foreground, target.WithAlpha(static_cast<std::uint8_t>(step * 255 / 20)));
        best                  = candidate;
        if (ContrastRatio(candidate, background) >= minRatio) {
            break;
        }
    }
    return best;
}

bool IsBlankGlyph(std::string_view character) {
    return character.empty() || character == " ";
}

} // namespace ned::ui
