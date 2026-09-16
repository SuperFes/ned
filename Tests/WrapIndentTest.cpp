#include <catch2/catch_test_macros.hpp>

#include "Editor/WrapIndent.h"

using ned::editor::SetWrapIndent;
using ned::editor::WrapIndent;

namespace {

// WrapIndent is process-wide state (see WrapIndent.h's own doc comment);
// every test that sets it must restore the default for the next test,
// guaranteed via RAII. Mirrors FinalNewlineTest.cpp's own
// FinalNewlineGuard exactly.
struct WrapIndentGuard {
    ~WrapIndentGuard() {
        SetWrapIndent(true);
    }
};

} // namespace

TEST_CASE("WrapIndent defaults to true", "[WrapIndent]") {
    const WrapIndentGuard guard;
    REQUIRE(WrapIndent());
}

TEST_CASE("SetWrapIndent/WrapIndent round-trip", "[WrapIndent]") {
    const WrapIndentGuard guard;
    SetWrapIndent(false);
    REQUIRE_FALSE(WrapIndent());
    SetWrapIndent(true);
    REQUIRE(WrapIndent());
}
