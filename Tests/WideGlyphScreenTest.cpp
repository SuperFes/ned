#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "UI/Widget.h"

using ned::ui::Cell;
using ned::ui::CellDraw;
using ned::ui::IsWideGlyph;
using ned::ui::ResolveRowDraws;

namespace {

std::vector<CellDraw> Resolve(const std::vector<std::string>& characters) {
    std::vector<Cell> row;
    for (const std::string& character : characters) {
        Cell cell;
        cell.character = character;
        row.push_back(cell);
    }
    std::vector<CellDraw> draws;
    ResolveRowDraws(row, draws);
    return draws;
}

constexpr CellDraw G = CellDraw::Glyph;
constexpr CellDraw C = CellDraw::Continuation;
constexpr CellDraw B = CellDraw::Blank;

} // namespace

TEST_CASE("IsWideGlyph asks the renderer's width", "[WideGlyphScreen]") {
    CHECK(IsWideGlyph("漢"));
    CHECK(IsWideGlyph("😀"));
    CHECK_FALSE(IsWideGlyph("a"));
    CHECK_FALSE(IsWideGlyph("é"));
    CHECK_FALSE(IsWideGlyph(""));
    CHECK_FALSE(IsWideGlyph("◁1B▷")); // several narrow glyphs in one cell is not a wide one
}

TEST_CASE("A wide glyph followed by its continuation draws once", "[WideGlyphScreen]") {
    CHECK(Resolve({"a", "漢", "", "b"}) == std::vector<CellDraw>{G, G, C, G});
    CHECK(Resolve({"漢", "", "字", ""}) == std::vector<CellDraw>{G, C, G, C});
}

TEST_CASE("Half a wide glyph draws as a blank", "[WideGlyphScreen]") {
    SECTION("clipped at the row's end") {
        CHECK(Resolve({"a", "漢"}) == std::vector<CellDraw>{G, B});
    }
    SECTION("its right half covered by another glyph") {
        CHECK(Resolve({"漢", "│", "b"}) == std::vector<CellDraw>{B, G, G});
    }
    SECTION("a continuation whose glyph was overwritten") {
        CHECK(Resolve({"x", "", "b"}) == std::vector<CellDraw>{G, B, G});
        CHECK(Resolve({"", "a"}) == std::vector<CellDraw>{B, G});
    }
    SECTION("two continuations in a row: only the first belongs to the glyph") {
        CHECK(Resolve({"漢", "", ""}) == std::vector<CellDraw>{G, C, B});
    }
}
