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

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Text/Rope.h"
#include "Text/RopeStorage.h"
#include "UI/BufferView/Internal.h"

using ned::ui::bufferview::RenderedInlayHint;
using ned::ui::detail::ByteOffsetForColumnInLine;
using ned::ui::detail::VisualColumn;

namespace {

// "Dim(std_plane, y)" with clangd's own parameter-name hints, which is the
// exact shape that exposed the bug.
const std::string kLine = "Dim(std_plane, y)";

std::vector<RenderedInlayHint> Hints() {
    return {
        RenderedInlayHint{.byteOffset = 4, .label = "plane:"}, // before "std_plane"
        RenderedInlayHint{.byteOffset = 15, .label = "y:"},    // before "y"
    };
}

} // namespace

TEST_CASE("VisualColumn counts the hints to point's left", "[InlayHint]") {
    const ned::text::RopeStorage content{ned::text::Rope(kLine)};

    SECTION("with no hints it is a plain column count") {
        REQUIRE(VisualColumn(content, 0, 4, 1000) == 4);
        REQUIRE(VisualColumn(content, 0, 13, 1000) == 13);
    }

    SECTION("a hint anchored at point does not count -- it renders after the cursor") {
        // Point sits immediately before "std_plane"; the hint renders to its
        // right, exactly where VS Code puts it, so the cursor stays put.
        REQUIRE(VisualColumn(content, 0, 4, 1000, {}, Hints()) == 4);
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
