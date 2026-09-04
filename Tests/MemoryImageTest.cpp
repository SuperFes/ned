#include <catch2/catch_test_macros.hpp>

#include "Editor/MemoryImage.h"

using ned::editor::ByteToGrayscale;
using ned::editor::ComputeMemoryImageLayout;

TEST_CASE("ComputeMemoryImageLayout reports a zeroed layout for empty input or a zero-width viewport", "[MemoryImage]") {
    REQUIRE(ComputeMemoryImageLayout(0, 40).pixelColumns == 0);
    REQUIRE(ComputeMemoryImageLayout(0, 40).pixelRows == 0);
    REQUIRE(ComputeMemoryImageLayout(128, 0).pixelColumns == 0);
    REQUIRE(ComputeMemoryImageLayout(128, 0).pixelRows == 0);
}

TEST_CASE("ComputeMemoryImageLayout never exceeds maxColumns and covers every byte", "[MemoryImage]") {
    const auto layout = ComputeMemoryImageLayout(128, 8);
    REQUIRE(layout.pixelColumns == 8);
    REQUIRE(layout.pixelColumns * layout.pixelRows >= 128);
    REQUIRE(layout.pixelColumns * (layout.pixelRows - 1) < 128);
}

TEST_CASE("ComputeMemoryImageLayout chooses a roughly-square grid when unconstrained", "[MemoryImage]") {
    const auto layout = ComputeMemoryImageLayout(100, 1000);
    REQUIRE(layout.pixelColumns == 10);
    REQUIRE(layout.pixelRows == 10);
}

TEST_CASE("ComputeMemoryImageLayout handles a single byte", "[MemoryImage]") {
    const auto layout = ComputeMemoryImageLayout(1, 40);
    REQUIRE(layout.pixelColumns == 1);
    REQUIRE(layout.pixelRows == 1);
}

TEST_CASE("ComputeMemoryImageLayout never chooses more columns than there are bytes", "[MemoryImage]") {
    // sqrt(3) rounds up to 2 columns -- still square-ish (2x2, covering the
    // 3rd byte with one row to spare) rather than a single 3-wide strip.
    const auto layout = ComputeMemoryImageLayout(3, 40);
    REQUIRE(layout.pixelColumns == 2);
    REQUIRE(layout.pixelRows == 2);
    REQUIRE(layout.pixelColumns <= 3);

    // A byte count smaller than its own square root's ceiling (2 bytes,
    // ceil(sqrt(2)) == 2) still never exceeds byteCount columns.
    const auto tiny = ComputeMemoryImageLayout(2, 40);
    REQUIRE(tiny.pixelColumns == 2);
    REQUIRE(tiny.pixelRows == 1);
}

TEST_CASE("ByteToGrayscale maps a byte to r == g == b == byte", "[MemoryImage]") {
    REQUIRE(ByteToGrayscale(0).r == 0);
    REQUIRE(ByteToGrayscale(0).g == 0);
    REQUIRE(ByteToGrayscale(0).b == 0);
    REQUIRE(ByteToGrayscale(255).r == 255);
    REQUIRE(ByteToGrayscale(128).g == 128);
}
