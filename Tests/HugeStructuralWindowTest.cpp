#include <catch2/catch_test_macros.hpp>

#include "Editor/HugeStructuralWindow.h"

using ned::editor::HugeStructuralWindowBytes;
using ned::editor::SetHugeStructuralWindowBytes;

namespace {

// Process-wide state -- restore the default via RAII, MaxHighlightBytesGuard's
// exact precedent (HighlightSettingsTest.cpp).
struct HugeStructuralWindowBytesGuard {
    ~HugeStructuralWindowBytesGuard() {
        SetHugeStructuralWindowBytes(4 * 1024 * 1024);
    }
};

} // namespace

TEST_CASE("HugeStructuralWindowBytes defaults to 4 MiB", "[HugeStructuralWindow]") {
    const HugeStructuralWindowBytesGuard guard;
    REQUIRE(HugeStructuralWindowBytes() == 4 * 1024 * 1024);
}

TEST_CASE("SetHugeStructuralWindowBytes/HugeStructuralWindowBytes round-trip", "[HugeStructuralWindow]") {
    const HugeStructuralWindowBytesGuard guard;
    SetHugeStructuralWindowBytes(1024);
    REQUIRE(HugeStructuralWindowBytes() == 1024);
}
