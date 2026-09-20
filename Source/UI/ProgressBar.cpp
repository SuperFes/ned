#include "ProgressBar.h"

#include <algorithm>
#include <cmath>

#include "Paint.h"
#include "ThemePaints.h"

namespace ned::ui {

namespace {

    // Eighth-blocks, one eighth through seven eighths. Index n is (n+1)/8 of
    // a cell filled from the left; a full cell is kFull below.
    constexpr std::string_view kEighths[] = {"▏", "▎", "▍", "▌", "▋", "▊", "▉"};
    constexpr std::string_view kFull      = "█";

    constexpr int kEighthsPerCell = 8;

    double Clamped(double fraction) {
        if (std::isnan(fraction)) {
            return 0.0; // a fraction nothing has measured yet, not an error to propagate
        }
        return std::clamp(fraction, 0.0, 1.0);
    }

    // The bar's length in eighths of a cell, which is the unit every
    // decision below is made in -- working in whole cells first and
    // recovering the remainder afterwards rounds twice and drifts.
    int FilledEighths(double fraction, int width) {
        return static_cast<int>(Clamped(fraction) * width * kEighthsPerCell);
    }

} // namespace

std::vector<std::string> ProgressGlyphs(double fraction, int width, std::string_view track) {
    if (width <= 0) {
        return {};
    }

    const int eighths   = FilledEighths(fraction, width);
    const int fullCells = eighths / kEighthsPerCell;
    const int remainder = eighths % kEighthsPerCell;

    std::vector<std::string> glyphs;
    glyphs.reserve(static_cast<std::size_t>(width));
    for (int x = 0; x < width; ++x) {
        if (x < fullCells) {
            glyphs.emplace_back(kFull);
        }
        else if (x == fullCells && remainder > 0) {
            glyphs.emplace_back(kEighths[remainder - 1]);
        }
        else {
            glyphs.emplace_back(track);
        }
    }
    return glyphs;
}

int ProgressFilledCells(double fraction, int width) {
    if (width <= 0) {
        return 0;
    }
    const int eighths = FilledEighths(fraction, width);
    // The partial leading cell counts: it is drawn in the fill colour, just
    // not across its whole width.
    return std::min(width, (eighths + kEighthsPerCell - 1) / kEighthsPerCell);
}

std::string ProgressPercentText(double fraction) {
    return std::to_string(static_cast<int>(Clamped(fraction) * 100.0)) + "%";
}

ProgressBar::ProgressBar(const Theme& theme) : theme_(theme) {
}

void ProgressBar::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    // Same clear/fill/glyphs order every other themed chrome row uses, and
    // cleared to ChromeBackdrop for the same reason: a bar sits beside
    // content, so a translucent fill should fade into it rather than dither.
    const Surface track = SurfaceFor(theme_, "progress");
    const Surface fill  = SurfaceFor(theme_, "progress.fill");
    ClearCanvas(c, ChromeBackdrop(theme_));
    Fill(c, track.fill);

    // Narrow boxes drop the trimmings rather than overflow. A bar needs a
    // handful of cells to mean anything; below that there is nothing useful
    // to draw and the row stays the plain themed track.
    constexpr int kMinimumBarCells = 4;

    const std::string percent      = ProgressPercentText(fraction);
    int               barWidth     = width;
    int               labelCells   = 0;
    int               percentCells = 0;

    if (!label.empty() && barWidth - static_cast<int>(label.size()) - 1 >= kMinimumBarCells) {
        labelCells = static_cast<int>(label.size()) + 1; // the separating space
        barWidth -= labelCells;
    }
    if (barWidth - static_cast<int>(percent.size()) - 1 >= kMinimumBarCells) {
        percentCells = static_cast<int>(percent.size()) + 1;
        barWidth -= percentCells;
    }
    if (barWidth < kMinimumBarCells) {
        return;
    }

    const std::vector<std::string> glyphs = ProgressGlyphs(fraction, barWidth);
    const int                      filled = ProgressFilledCells(fraction, barWidth);

    const int row = height / 2; // centred, so a taller box than one row still reads

    const auto put = [&](int x, std::string_view glyph, const Surface& surface, const Color& fallback) {
        if (x < 0 || x >= width) {
            return;
        }
        const Point at{.x = x, .y = row};
        Cell&       cell      = c[at];
        cell.character        = std::string(glyph);
        cell.foreground_color = TextColourAt(surface, c, at, fallback);
    };

    int x = 0;
    if (labelCells > 0) {
        for (std::size_t i = 0; i < label.size(); ++i, ++x) {
            put(x, std::string_view(&label[i], 1), track, theme_.defaultForeground);
        }
        ++x; // the separating space, left as the track's own fill
    }

    for (int i = 0; i < barWidth; ++i, ++x) {
        // The filled run takes the fill surface's colour; the track glyphs
        // past it stay in the track's, so the two read apart even when a
        // theme gives them the same shape.
        const bool inFill = i < filled;
        put(x, glyphs[static_cast<std::size_t>(i)], inFill ? fill : track,
            inFill ? theme_.modeLineFocusedGradientStart : theme_.defaultForeground);
    }

    if (percentCells > 0) {
        ++x;
        for (char ch : percent) {
            put(x++, std::string_view(&ch, 1), track, theme_.defaultForeground);
        }
    }

    ApplyTextFade(c, track);
}

} // namespace ned::ui
