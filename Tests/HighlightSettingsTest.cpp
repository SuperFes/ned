#include <catch2/catch_test_macros.hpp>

#include "Editor/HighlightSettings.h"

using ned::editor::MaxHighlightBytes;
using ned::editor::SetMaxHighlightBytes;

namespace {

// Process-wide state -- restore the default via RAII, TabWidthGuard's exact
// precedent.
struct MaxHighlightBytesGuard {
    ~MaxHighlightBytesGuard() {
        SetMaxHighlightBytes(8 * 1024 * 1024);
    }
};

} // namespace

TEST_CASE("MaxHighlightBytes defaults to 8 MiB", "[HighlightSettings]") {
    const MaxHighlightBytesGuard guard;
    REQUIRE(MaxHighlightBytes() == 8 * 1024 * 1024);
}

TEST_CASE("SetMaxHighlightBytes/MaxHighlightBytes round-trip, including 0 (disable)", "[HighlightSettings]") {
    const MaxHighlightBytesGuard guard;
    SetMaxHighlightBytes(1024);
    REQUIRE(MaxHighlightBytes() == 1024);
    SetMaxHighlightBytes(0);
    REQUIRE(MaxHighlightBytes() == 0);
}
