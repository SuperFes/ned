#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "Editor/DiffRefreshSettings.h"

using ned::editor::DiffRefreshDebounce;
using ned::editor::SetDiffRefreshDebounceMs;

namespace {

// Process-wide state (see DiffRefreshSettings.h's own doc comment); every
// test that sets one must restore the default for the next test, mirroring
// TabWidthTest.cpp's own TabWidthGuard exactly.
struct DiffRefreshDebounceGuard {
    ~DiffRefreshDebounceGuard() {
        SetDiffRefreshDebounceMs(1200);
    }
};

} // namespace

TEST_CASE("DiffRefreshDebounce defaults to 1200ms", "[DiffRefreshSettings]") {
    const DiffRefreshDebounceGuard guard;
    REQUIRE(DiffRefreshDebounce() == std::chrono::milliseconds(1200));
}

TEST_CASE("SetDiffRefreshDebounceMs/DiffRefreshDebounce round-trip", "[DiffRefreshSettings]") {
    const DiffRefreshDebounceGuard guard;
    SetDiffRefreshDebounceMs(500);
    REQUIRE(DiffRefreshDebounce() == std::chrono::milliseconds(500));
    SetDiffRefreshDebounceMs(3000);
    REQUIRE(DiffRefreshDebounce() == std::chrono::milliseconds(3000));
}

TEST_CASE("SetDiffRefreshDebounceMs clamps a non-positive value to 1ms", "[DiffRefreshSettings]") {
    const DiffRefreshDebounceGuard guard;
    SetDiffRefreshDebounceMs(0);
    REQUIRE(DiffRefreshDebounce() == std::chrono::milliseconds(1));
    SetDiffRefreshDebounceMs(-100);
    REQUIRE(DiffRefreshDebounce() == std::chrono::milliseconds(1));
}
