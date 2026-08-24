#include <catch2/catch_test_macros.hpp>

#include "Editor/TrimOnSave.h"

using ned::editor::SetTrimTrailingWhitespaceOnSave;
using ned::editor::TrimTrailingWhitespaceOnSave;

namespace {

// TrimOnSave is process-wide state (see TrimOnSave.h's own doc comment);
// every test that sets it must restore the default for the next test,
// guaranteed via RAII. Mirrors FinalNewlineTest.cpp's own guard exactly.
struct TrimOnSaveGuard {
    ~TrimOnSaveGuard() { SetTrimTrailingWhitespaceOnSave(true); }
};

} // namespace

TEST_CASE("TrimTrailingWhitespaceOnSave defaults to true", "[TrimOnSave]") {
    const TrimOnSaveGuard guard;
    REQUIRE(TrimTrailingWhitespaceOnSave());
}

TEST_CASE("SetTrimTrailingWhitespaceOnSave/TrimTrailingWhitespaceOnSave round-trip", "[TrimOnSave]") {
    const TrimOnSaveGuard guard;
    SetTrimTrailingWhitespaceOnSave(false);
    REQUIRE_FALSE(TrimTrailingWhitespaceOnSave());
    SetTrimTrailingWhitespaceOnSave(true);
    REQUIRE(TrimTrailingWhitespaceOnSave());
}
