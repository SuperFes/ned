// ui::ProgressBar -- the determinate bar, and the glyph run the mode line
// shares with it.
//
// The sub-cell behaviour is the part worth pinning: a bar that advanced only
// a whole cell at a time would sit visibly still through 10% of a long save,
// which is exactly when a user starts wondering whether it has hung. The
// eighth-block cases below are what prove it moves in between.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include "UI/ProgressBar.h"
#include "UI/Theme.h"

using ned::ui::ProgressBar;
using ned::ui::ProgressFilledCells;
using ned::ui::ProgressGlyphs;
using ned::ui::ProgressPercentText;

namespace {

std::string Joined(double fraction, int width, std::string_view track = " ") {
    std::string out;
    for (const std::string& glyph : ProgressGlyphs(fraction, width, track)) {
        out += glyph;
    }
    return out;
}

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

} // namespace

TEST_CASE("ProgressGlyphs fills whole cells at exact boundaries", "[ProgressBar]") {
    REQUIRE(Joined(0.0, 4, "-") == "----");
    REQUIRE(Joined(0.25, 4, "-") == "█---");
    REQUIRE(Joined(0.5, 4, "-") == "██--");
    REQUIRE(Joined(1.0, 4, "-") == "████");
}

TEST_CASE("ProgressGlyphs advances in eighths between whole cells", "[ProgressBar]") {
    // One cell wide, so each eighth is the leading cell and nothing else --
    // the clearest statement of the sub-cell rule.
    REQUIRE(Joined(1.0 / 8.0, 1, "-") == "▏");
    REQUIRE(Joined(2.0 / 8.0, 1, "-") == "▎");
    REQUIRE(Joined(3.0 / 8.0, 1, "-") == "▍");
    REQUIRE(Joined(4.0 / 8.0, 1, "-") == "▌");
    REQUIRE(Joined(5.0 / 8.0, 1, "-") == "▋");
    REQUIRE(Joined(6.0 / 8.0, 1, "-") == "▊");
    REQUIRE(Joined(7.0 / 8.0, 1, "-") == "▉");
    REQUIRE(Joined(8.0 / 8.0, 1, "-") == "█");
}

TEST_CASE("ProgressGlyphs moves within a single cell of a wider bar", "[ProgressBar]") {
    // Ten cells: 1% apart is far less than a cell, and must still differ.
    REQUIRE(Joined(0.51, 10, "-") != Joined(0.55, 10, "-"));
    REQUIRE(Joined(0.50, 10, "-") == "█████-----");
    REQUIRE(Joined(0.55, 10, "-") == "█████▌----"); // five and a half cells: the sixth is four eighths
}

TEST_CASE("ProgressGlyphs clamps anything outside 0..1", "[ProgressBar]") {
    REQUIRE(Joined(-1.0, 4, "-") == "----");
    REQUIRE(Joined(2.0, 4, "-") == "████");
    REQUIRE(Joined(std::nan(""), 4, "-") == "----"); // unmeasured, not an error to propagate
}

TEST_CASE("ProgressGlyphs returns exactly width cells, or none at all", "[ProgressBar]") {
    REQUIRE(ProgressGlyphs(0.5, 7).size() == 7);
    REQUIRE(ProgressGlyphs(0.5, 0).empty());
    REQUIRE(ProgressGlyphs(0.5, -3).empty());
}

TEST_CASE("ProgressFilledCells counts the partial leading cell", "[ProgressBar]") {
    REQUIRE(ProgressFilledCells(0.0, 10) == 0);
    REQUIRE(ProgressFilledCells(0.5, 10) == 5);
    REQUIRE(ProgressFilledCells(0.55, 10) == 6); // five full plus the partial sixth
    REQUIRE(ProgressFilledCells(1.0, 10) == 10);
    REQUIRE(ProgressFilledCells(2.0, 10) == 10); // never past the end
    REQUIRE(ProgressFilledCells(0.5, 0) == 0);
}

TEST_CASE("ProgressPercentText never reads 100% before it is done", "[ProgressBar]") {
    REQUIRE(ProgressPercentText(0.0) == "0%");
    REQUIRE(ProgressPercentText(0.25) == "25%");
    REQUIRE(ProgressPercentText(0.999) == "99%"); // rounds toward zero, deliberately
    REQUIRE(ProgressPercentText(1.0) == "100%");
    REQUIRE(ProgressPercentText(2.0) == "100%");
}

TEST_CASE("ProgressBar paints a label, a bar and a percentage", "[ProgressBar]") {
    const ned::ui::Theme theme = ned::ui::DarkTheme();
    ProgressBar          bar(theme);
    bar.label    = "Saving";
    bar.fraction = 0.5;

    ned::ui::Screen screen(30, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 0});
    bar.Paint(canvas);

    const std::string row = RowText(screen, 0, 30);
    REQUIRE(row.find("Saving") != std::string::npos);
    REQUIRE(row.find("50%") != std::string::npos);
    REQUIRE(row.find("█") != std::string::npos);
}

TEST_CASE("ProgressBar drops the percentage, then the label, as the box narrows", "[ProgressBar]") {
    const ned::ui::Theme theme = ned::ui::DarkTheme();

    const auto rowAtWidth = [&theme](int width) {
        ProgressBar bar(theme);
        bar.label    = "Saving";
        bar.fraction = 0.5;
        ned::ui::Screen screen(width, 1);
        ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = 0});
        bar.Paint(canvas);
        std::string out;
        for (int col = 0; col < width; ++col) {
            out += screen.PixelAt(col, 0).character;
        }
        return out;
    };

    // Roomy: everything fits.
    const std::string wide = rowAtWidth(30);
    REQUIRE(wide.find("Saving") != std::string::npos);
    REQUIRE(wide.find("50%") != std::string::npos);

    // Tighter: the label survives, the percentage is the first to go.
    const std::string medium = rowAtWidth(14);
    REQUIRE(medium.find("Saving") != std::string::npos);
    REQUIRE(medium.find("50%") == std::string::npos);
    REQUIRE(medium.find("█") != std::string::npos);

    // Tighter still: only the bar itself is left.
    const std::string narrow = rowAtWidth(8);
    REQUIRE(narrow.find("Saving") == std::string::npos);
    REQUIRE(narrow.find("█") != std::string::npos);
}

TEST_CASE("ProgressBar paints nothing into a box too small for a bar", "[ProgressBar]") {
    const ned::ui::Theme theme = ned::ui::DarkTheme();
    ProgressBar          bar(theme);
    bar.fraction = 0.5;

    ned::ui::Screen screen(3, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 2, .y_min = 0, .y_max = 0});
    bar.Paint(canvas); // must not overflow or assert

    REQUIRE(RowText(screen, 0, 3).find("█") == std::string::npos);
}
