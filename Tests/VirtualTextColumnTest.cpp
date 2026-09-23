//
// Inlay hints occupy real cells *before* the character they annotate, so
// every piece of column arithmetic has to count them or the cursor drifts
// left of the character it is on -- by the total width of the hints earlier
// in the line. That drift was a real, reported bug: an Enter split appeared
// in the wrong place, and the horizontal-scroll decision under-estimated how
// far right point actually sat.
//
// These pin the two directions against each other: VisualColumn (offset ->
// column) and ByteOffsetForColumnInLine (column -> offset).
//
// A colour swatch is the second kind of virtual text and rides the same span
// list for exactly this reason, so the last case here pins its width through
// the same arithmetic.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Text/Rope.h"
#include "Text/RopeStorage.h"
#include "UI/BufferView/Internal.h"

using ned::ui::bufferview::RenderedVirtualText;
using ned::ui::detail::ByteOffsetForColumnInLine;
using ned::ui::detail::VisualColumn;

namespace {

// "Dim(std_plane, y)" with clangd's own parameter-name hints, which is the
// exact shape that exposed the bug.
const std::string kLine = "Dim(std_plane, y)";

std::vector<RenderedVirtualText> Hints() {
    return {
        RenderedVirtualText{.byteOffset = 4, .label = "plane:"}, // before "std_plane"
        RenderedVirtualText{.byteOffset = 15, .label = "y:"},    // before "y"
    };
}

} // namespace

TEST_CASE("VisualColumn counts the hints to point's left", "[InlayHint]") {
    const ned::text::RopeStorage content{ned::text::Rope(kLine)};

    SECTION("with no hints it is a plain column count") {
        REQUIRE(VisualColumn(content, 0, 4, 1000) == 4);
        REQUIRE(VisualColumn(content, 0, 13, 1000) == 13);
    }

    SECTION("a hint anchored at point counts too -- it renders before the real character") {
        // Point sits immediately before "std_plane". EmitVirtualText draws the
        // "plane:" hint first and only then the real byte still at this same
        // offset, so the real character's own column sits past the hint --
        // and the cursor has to land there too, or it draws on top of the
        // hint's own glyphs instead of the character it's actually on.
        REQUIRE(VisualColumn(content, 0, 4, 1000, {}, Hints()) == 10);
    }

    SECTION("a hint before point pushes it right by the hint's width") {
        // "Dim(" + "plane:" + "std_plane" = 4 + 6 + 9
        REQUIRE(VisualColumn(content, 0, 13, 1000, {}, Hints()) == 19);
    }

    SECTION("hints accumulate") {
        // ...and then "," + " " + "y:" before the final "y".
        REQUIRE(VisualColumn(content, 0, 16, 1000, {}, Hints()) == 24);
    }
}

TEST_CASE("ByteOffsetForColumnInLine is VisualColumn's inverse under hints", "[InlayHint]") {
    const ned::text::RopeStorage content{ned::text::Rope(kLine)};
    const auto                   hints = Hints();

    SECTION("round-trips every offset in the line") {
        for (std::size_t offset = 0; offset <= kLine.size(); ++offset) {
            const auto column = VisualColumn(content, 0, offset, 1000, {}, hints);
            REQUIRE(column.has_value());
            INFO("offset " << offset << " column " << *column);
            REQUIRE(ByteOffsetForColumnInLine(content, 0, kLine.size(), static_cast<std::size_t>(*column), 4, {},
                                              hints) == offset);
        }
    }

    SECTION("a click on the hint itself lands on the character it annotates") {
        // Columns 4..9 are the "plane:" hint; every one of them resolves to
        // the "std_plane" it is describing rather than to some other token.
        for (std::size_t column = 4; column < 10; ++column) {
            INFO("column " << column);
            REQUIRE(ByteOffsetForColumnInLine(content, 0, kLine.size(), column, 4, {}, hints) == 4);
        }
    }

    SECTION("without hints it is unchanged") {
        REQUIRE(ByteOffsetForColumnInLine(content, 0, kLine.size(), 13, 4, {}) == 13);
    }
}

// Paint()'s horizontal-scroll fast-forward is the third consumer of the same
// column arithmetic, and the one that was missing hint accounting: it
// consumed LeftColumn() columns of *characters* where the drawing loop would
// have spent some of them on hints, so the row was drawn from further into
// the line than the cursor was placed against. Live symptom (2026-09-22):
// point at the end of a long line rendered 64 columns past the last painted
// character, 64 being the total width of the hints scrolled off to the left.
TEST_CASE("SkipToColumn lands where VisualColumn says it should", "[InlayHint]") {
    using ned::ui::detail::SkipToColumn;
    const ned::text::RopeStorage content{ned::text::Rope(kLine)};
    const std::size_t           end = kLine.size();

    SECTION("with no hints, skipping N columns lands at column N") {
        for (int target = 0; target <= static_cast<int>(end); ++target) {
            const auto skip = SkipToColumn(content, 0, end, target);
            REQUIRE(skip.columns == target);
            REQUIRE(VisualColumn(content, 0, skip.offset, 1000) == target);
        }
    }

    SECTION("with hints, the offset it stops at really is at the column it reports") {
        const std::vector<RenderedVirtualText> hints = Hints();
        // The two measure deliberately different edges of the same offset:
        // SkipToColumn reports where *rendering* of skip.offset begins,
        // which is before any hint anchored there, while VisualColumn
        // reports where the real character lands, which is after it. That
        // gap is the whole reason Paint sets rowStartColumn from the former
        // and places the cursor from the latter, so the identity that binds
        // them has to name it rather than assume equality.
        //
        // 8 is "plane:" + "y:" -- the whole virtual width on this line, so
        // the walk crosses both and the pre-fix drift would be widest past
        // them.
        for (int target = 0; target <= static_cast<int>(end) + 8; ++target) {
            const auto skip    = SkipToColumn(content, 0, end, target, {}, hints);
            int        expected = skip.columns;
            if (const RenderedVirtualText* hint = ned::ui::detail::VirtualTextStartingAt(hints, skip.offset)) {
                expected += ned::ui::detail::DisplayColumns(hint->label, skip.columns);
            }
            REQUIRE(VisualColumn(content, 0, skip.offset, 1000, {}, hints) == expected);
            // Never stops short: the row would start left of the scroll.
            REQUIRE((skip.columns >= target || skip.offset == end));
        }
    }

    SECTION("the hint's own width is included, not skipped over") {
        const std::vector<RenderedVirtualText> hints = Hints();
        // Column 4 is where "plane:" begins; asking for 4 columns must stop
        // before it, and asking for 5 must have paid its full 6.
        REQUIRE(SkipToColumn(content, 0, end, 4, {}, hints).columns == 4);
        const auto past = SkipToColumn(content, 0, end, 5, {}, hints);
        REQUIRE(past.columns == 4 + 6 + 1); // "plane:" then the 's' of "std_plane"
    }
}

// A swatch is one cell ahead of whatever label the same offset carries, and
// every column walk has to agree on that -- which is why they all ask
// VirtualTextColumns() rather than measuring the label themselves.
TEST_CASE("A colour swatch is one column, ahead of any label at the same offset", "[InlayHint][ColorSwatch]") {
    using ned::ui::detail::VirtualTextColumns;
    const ned::text::RopeStorage content{ned::text::Rope(kLine)};

    const RenderedVirtualText swatchOnly{.byteOffset = 4, .swatch = ned::ui::Color::RGB(0xff00aa)};
    const RenderedVirtualText swatchAndLabel{
        .byteOffset = 4, .label = "plane:", .swatch = ned::ui::Color::RGB(0xff00aa)};

    CHECK(VirtualTextColumns(RenderedVirtualText{.byteOffset = 4, .label = "plane:"}, 0) == 6);
    CHECK(VirtualTextColumns(swatchOnly, 0) == 1);
    CHECK(VirtualTextColumns(swatchAndLabel, 0) == 7);

    SECTION("VisualColumn counts the swatch cell") {
        REQUIRE(VisualColumn(content, 0, 4, 1000, {}, {swatchOnly}) == 5);
        REQUIRE(VisualColumn(content, 0, 13, 1000, {}, {swatchOnly}) == 14);
    }

    SECTION("and ByteOffsetForColumnInLine inverts it") {
        for (std::size_t offset = 0; offset <= kLine.size(); ++offset) {
            const auto column = VisualColumn(content, 0, offset, 1000, {}, {swatchAndLabel});
            REQUIRE(column.has_value());
            INFO("offset " << offset << " column " << *column);
            REQUIRE(ByteOffsetForColumnInLine(content, 0, kLine.size(), static_cast<std::size_t>(*column), 4, {},
                                              {swatchAndLabel}) == offset);
        }
    }
}
