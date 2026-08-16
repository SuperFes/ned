#include <catch2/catch_test_macros.hpp>

#include "Editor/FinalNewline.h"

using ned::editor::EnsureFinalNewline;
using ned::editor::SetEnsureFinalNewline;

namespace {

// FinalNewline is process-wide state (see FinalNewline.h's own doc
// comment); every test that sets it must restore the default for the next
// test, guaranteed via RAII. Mirrors TabWidthTest.cpp's own TabWidthGuard
// exactly.
struct FinalNewlineGuard {
    ~FinalNewlineGuard() { SetEnsureFinalNewline(true); }
};

} // namespace

TEST_CASE("EnsureFinalNewline defaults to true", "[FinalNewline]") {
    const FinalNewlineGuard guard;
    REQUIRE(EnsureFinalNewline());
}

TEST_CASE("SetEnsureFinalNewline/EnsureFinalNewline round-trip", "[FinalNewline]") {
    const FinalNewlineGuard guard;
    SetEnsureFinalNewline(false);
    REQUIRE_FALSE(EnsureFinalNewline());
    SetEnsureFinalNewline(true);
    REQUIRE(EnsureFinalNewline());
}
