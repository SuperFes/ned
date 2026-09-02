#include <catch2/catch_test_macros.hpp>

#include "Editor/FillColumn.h"

using ned::editor::FillColumn;
using ned::editor::SetFillColumn;

namespace {

    // FillColumn is process-wide state (see FillColumn.h's own doc comment);
    // every test that sets one must restore the default for the next test,
    // guaranteed via RAII the same way TabWidthTest.cpp's own
    // TabWidthGuard does.
    struct FillColumnGuard {
        ~FillColumnGuard() {
            SetFillColumn(70);
        }
    };

} // namespace

TEST_CASE("FillColumn defaults to 70", "[FillColumn]") {
    const FillColumnGuard guard;
    REQUIRE(FillColumn() == 70);
}

TEST_CASE("SetFillColumn/FillColumn round-trip", "[FillColumn]") {
    const FillColumnGuard guard;
    SetFillColumn(40);
    REQUIRE(FillColumn() == 40);
    SetFillColumn(100);
    REQUIRE(FillColumn() == 100);
}

TEST_CASE("SetFillColumn clamps a non-positive width to 1", "[FillColumn]") {
    const FillColumnGuard guard;
    SetFillColumn(0);
    REQUIRE(FillColumn() == 1);
    SetFillColumn(-5);
    REQUIRE(FillColumn() == 1);
}
