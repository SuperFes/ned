#include <catch2/catch_test_macros.hpp>

#include <string>

#include "UI/Compositing.h"
#include "UI/Widget.h"

using ned::ui::AlphaPolicy;
using ned::ui::BlendOver;
using ned::ui::Cell;
using ned::ui::Color;
using ned::ui::DitherGlyph;
using ned::ui::IsBlankGlyph;
using ned::ui::Screen;
using ned::ui::TintToward;

namespace {

// A wash: a background color, no glyph of its own, which is what lets it
// tint a row of real text instead of erasing it.
Cell Wash(Color background) {
    Cell cell;
    cell.character        = "";
    cell.background_color = background;
    return cell;
}

Cell Glyph(std::string character, Color foreground, Color background = Color::Default) {
    Cell cell;
    cell.character        = std::move(character);
    cell.foreground_color = foreground;
    cell.background_color = background;
    return cell;
}

} // namespace

TEST_CASE("Color alpha defaults to opaque so nothing predating it changed meaning", "[Compositing]") {
    REQUIRE(Color::RGB(0x112233).alpha == 255);
    REQUIRE(Color::RGB(0x112233).Opaque());
    REQUIRE(Color::RGB(0x112233) == Color::RGBA(0x112233FF));
    REQUIRE(Color::Default.Opaque());

    REQUIRE(Color::RGBA(0x11223380).alpha == 0x80);
    REQUIRE_FALSE(Color::RGBA(0x11223380).Opaque());
    REQUIRE(Color::RGB(0x112233).WithAlpha(0).alpha == 0);
}

TEST_CASE("Only TrueColor is composable", "[Compositing]") {
    REQUIRE(Color::RGB(0x000000).Composable());
    REQUIRE(Color::Red.Composable()); // the named colours are real RGB now
    REQUIRE_FALSE(Color::Default.Composable());
    // A palette index survives only as transport for the embedded
    // terminal's own indexed colours, and has no RGB we could blend.
    REQUIRE_FALSE(Color::Palette(1).Composable());
}

TEST_CASE("BlendOver resolves alpha away", "[Compositing]") {
    const Color black = Color::RGB(0x000000);
    const Color white = Color::RGB(0xFFFFFF);

    SECTION("endpoints") {
        REQUIRE(BlendOver(black, white.WithAlpha(255)) == white);
        REQUIRE(BlendOver(black, white.WithAlpha(0)) == black);
    }

    SECTION("half way") {
        const Color mid = BlendOver(black, white.WithAlpha(128));
        REQUIRE(mid.kind == Color::Kind::TrueColor);
        REQUIRE(static_cast<int>(mid.red) == 128);
        REQUIRE(static_cast<int>(mid.green) == 128);
        REQUIRE(static_cast<int>(mid.blue) == 128);
    }

    SECTION("the result is always opaque -- a screen cell is not a layer") {
        REQUIRE(BlendOver(black, white.WithAlpha(40)).Opaque());
    }

    SECTION("an uncomposable destination yields the source, not an invented backdrop") {
        REQUIRE(BlendOver(Color::Default, white.WithAlpha(64)) == white);
    }
}

TEST_CASE("TintToward leaves what it cannot move", "[Compositing]") {
    const Color text = Color::RGB(0x808080);
    REQUIRE(static_cast<int>(TintToward(text, Color::RGB(0xFFFFFF).WithAlpha(128)).red) == 192);
    REQUIRE(TintToward(text, Color::RGB(0xFFFFFF).WithAlpha(0)) == text);
    REQUIRE(TintToward(Color::Default, Color::RGB(0xFFFFFF).WithAlpha(128)) == Color::Default);

    SECTION("an opaque wash replaces even an unknown destination") {
        // "Nothing of the original survives" needs no knowledge of the
        // original -- and a caller writing a space in a colour depends on it.
        REQUIRE(TintToward(Color::Default, Color::RGB(0xFFFFFF)) == Color::RGB(0xFFFFFF));
    }
}

TEST_CASE("A source space is a glyph; a destination space is nothing", "[Compositing]") {
    Screen screen(2, 1);

    // Writing a space in a colour must set that colour, or every blank cell
    // of a chrome row would keep whatever foreground was there before.
    Cell space;
    space.character        = " ";
    space.foreground_color = Color::RGB(0x40C080);
    screen.Blend(0, 0, space);
    REQUIRE(screen.PixelAt(0, 0).character == " ");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == Color::RGB(0x40C080));

    // But a destination holding only a space is still empty enough to dither
    // into.
    screen.Blend(0, 0, Wash(Color::RGB(0x40C080).WithAlpha(128)));
    REQUIRE_FALSE(IsBlankGlyph(screen.PixelAt(0, 0).character));
}

TEST_CASE("DitherGlyph spans nothing to a full block", "[Compositing]") {
    REQUIRE(DitherGlyph(0.0, 0, 0).empty());
    REQUIRE(DitherGlyph(1.0, 0, 0) == "█");
    REQUIRE(DitherGlyph(0.5, 3, 4).size() == 3); // one 3-byte braille cell

    SECTION("deterministic -- static texture, never per-frame shimmer") {
        REQUIRE(DitherGlyph(0.4, 7, 9) == DitherGlyph(0.4, 7, 9));
    }

    SECTION("dispersed -- neighbouring cells differ, so a ramp does not stripe") {
        REQUIRE(DitherGlyph(0.3, 0, 0) != DitherGlyph(0.3, 1, 0));
    }

    SECTION("monotone -- more coverage is never fewer dots") {
        int previous = 0;
        for (int step = 0; step <= 8; ++step) {
            const std::string glyph = DitherGlyph(step / 8.0, 2, 2);
            const int         lit   = glyph.empty() ? 0 : (glyph == "█" ? 9 : 1 + step);
            REQUIRE(lit >= previous);
            previous = lit;
        }
    }

    SECTION("negative coordinates still index the matrix") {
        REQUIRE_NOTHROW(DitherGlyph(0.5, -3, -7));
    }
}

TEST_CASE("IsBlankGlyph treats a space as empty", "[Compositing]") {
    REQUIRE(IsBlankGlyph(""));
    REQUIRE(IsBlankGlyph(" "));
    REQUIRE_FALSE(IsBlankGlyph("x"));
}

TEST_CASE("Rule 1: an opaque blend is exactly an assignment", "[Compositing]") {
    Screen screen(4, 2);
    screen.PixelAt(1, 0).background_color = Color::RGB(0x101010);
    screen.PixelAt(1, 0).character        = "x";
    screen.PixelAt(1, 0).foreground_color = Color::RGB(0xEEEEEE);

    screen.Blend(1, 0, Wash(Color::RGB(0x203040)));

    REQUIRE(screen.PixelAt(1, 0).background_color == Color::RGB(0x203040));
    REQUIRE(screen.PixelAt(1, 0).character == "x"); // an empty src glyph preserves the text
    REQUIRE(screen.PixelAt(1, 0).foreground_color == Color::RGB(0xEEEEEE));
}

TEST_CASE("Rule 2: a known background composites exactly", "[Compositing]") {
    Screen screen(4, 2);
    screen.PixelAt(0, 0).background_color = Color::RGB(0x000000);
    screen.PixelAt(0, 0).character        = "y";

    screen.Blend(0, 0, Wash(Color::RGB(0xFFFFFF).WithAlpha(128)));

    REQUIRE(static_cast<int>(screen.PixelAt(0, 0).background_color.red) == 128);
    REQUIRE(screen.PixelAt(0, 0).background_color.Opaque());
    REQUIRE(screen.PixelAt(0, 0).character == "y");
}

TEST_CASE("Rule 3: an empty cell over the terminal background dithers", "[Compositing]") {
    Screen screen(4, 2);
    REQUIRE(screen.PixelAt(2, 1).background_color == Color::Default);

    screen.Blend(2, 1, Wash(Color::RGB(0x40C080).WithAlpha(128)));

    const Cell& cell = screen.PixelAt(2, 1);
    REQUIRE(cell.background_color == Color::Default); // still see-through, which is the point
    REQUIRE_FALSE(IsBlankGlyph(cell.character));
    REQUIRE(cell.foreground_color == Color::RGB(0x40C080));
}

TEST_CASE("Rule 4: a glyph over the terminal background tints the foreground", "[Compositing]") {
    Screen screen(4, 2);
    screen.PixelAt(0, 1).character        = "z";
    screen.PixelAt(0, 1).foreground_color = Color::RGB(0x000000);

    screen.Blend(0, 1, Wash(Color::RGB(0xFFFFFF).WithAlpha(128)));

    const Cell& cell = screen.PixelAt(0, 1);
    REQUIRE(cell.character == "z");
    REQUIRE(cell.background_color == Color::Default);
    REQUIRE(cell.foreground_color.red == 128);
}

TEST_CASE("Rule 5: AlphaPolicy::Opaque gives up transparency deliberately", "[Compositing]") {
    Screen screen(4, 2);
    screen.Blend(0, 0, Wash(Color::RGB(0x40C080).WithAlpha(64)), AlphaPolicy::Opaque);

    REQUIRE(screen.PixelAt(0, 0).background_color == Color::RGB(0x40C080));
    REQUIRE(IsBlankGlyph(screen.PixelAt(0, 0).character));
}

TEST_CASE("Policies steer what happens over a transparent background", "[Compositing]") {
    SECTION("Skip leaves the cell completely alone") {
        Screen screen(2, 1);
        screen.Blend(0, 0, Wash(Color::RGB(0x40C080).WithAlpha(128)), AlphaPolicy::Skip);
        REQUIRE(IsBlankGlyph(screen.PixelAt(0, 0).character));
        REQUIRE(screen.PixelAt(0, 0).background_color == Color::Default);
    }

    SECTION("Dither declines glyph cells -- a pattern never eats text") {
        Screen screen(2, 1);
        screen.PixelAt(0, 0).character        = "q";
        screen.PixelAt(0, 0).foreground_color = Color::RGB(0x000000);
        screen.Blend(0, 0, Wash(Color::RGB(0xFFFFFF).WithAlpha(128)), AlphaPolicy::Dither);
        REQUIRE(screen.PixelAt(0, 0).character == "q");
        REQUIRE(screen.PixelAt(0, 0).foreground_color == Color::RGB(0x000000));
    }

    SECTION("TintText never dithers, even in an empty cell") {
        Screen screen(2, 1);
        screen.PixelAt(0, 0).foreground_color = Color::RGB(0x000000);
        screen.Blend(0, 0, Wash(Color::RGB(0xFFFFFF).WithAlpha(128)), AlphaPolicy::TintText);
        REQUIRE(IsBlankGlyph(screen.PixelAt(0, 0).character));
        REQUIRE(static_cast<int>(screen.PixelAt(0, 0).foreground_color.red) == 128);
    }
}

TEST_CASE("A translucent foreground with no glyph is a Fade", "[Compositing]") {
    Screen screen(2, 1);
    screen.PixelAt(0, 0).character        = "f";
    screen.PixelAt(0, 0).foreground_color = Color::RGB(0xFFFFFF);
    screen.PixelAt(0, 0).background_color = Color::RGB(0x000000);

    Cell fade;
    fade.character        = "";
    fade.foreground_color = Color::RGB(0x000000).WithAlpha(128); // pull halfway toward black
    screen.Blend(0, 0, fade);

    REQUIRE(screen.PixelAt(0, 0).character == "f");
    REQUIRE(screen.PixelAt(0, 0).background_color == Color::RGB(0x000000));
    REQUIRE(static_cast<int>(screen.PixelAt(0, 0).foreground_color.red) == 127);
}

TEST_CASE("A source glyph replaces glyph and traits", "[Compositing]") {
    Screen screen(2, 1);
    screen.PixelAt(0, 0).character = "old";
    screen.PixelAt(0, 0).bold      = true;

    screen.Blend(0, 0, Glyph("new", Color::RGB(0x123456)));

    REQUIRE(screen.PixelAt(0, 0).character == "new");
    REQUIRE_FALSE(screen.PixelAt(0, 0).bold);
    REQUIRE(screen.PixelAt(0, 0).foreground_color == Color::RGB(0x123456));
}

TEST_CASE("A translucent glyph color composites against the resolved background", "[Compositing]") {
    Screen screen(2, 1);
    screen.PixelAt(0, 0).background_color = Color::RGB(0x000000);

    screen.Blend(0, 0, Glyph("g", Color::RGB(0xFFFFFF).WithAlpha(128), Color::RGB(0x000000)));

    REQUIRE(static_cast<int>(screen.PixelAt(0, 0).foreground_color.red) == 128);
}

TEST_CASE("Out-of-range coordinates are ignored, matching Canvas's own clipping", "[Compositing]") {
    Screen screen(2, 1);
    REQUIRE_NOTHROW(screen.Blend(-1, 0, Wash(Color::RGB(0xFFFFFF))));
    REQUIRE_NOTHROW(screen.Blend(0, 5, Wash(Color::RGB(0xFFFFFF))));
    REQUIRE_NOTHROW(screen.Blend(9, 9, Wash(Color::RGB(0xFFFFFF))));
    REQUIRE(screen.PixelAt(0, 0).background_color == Color::Default);
}
