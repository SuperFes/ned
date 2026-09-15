#include <catch2/catch_test_macros.hpp>

#include "Editor/Format.h"
#include "Editor/FinalNewline.h"
#include "Editor/MaxConsecutiveBlankLines.h"
#include "Editor/TrimOnSave.h"
#include "Text/Buffer.h"

using ned::editor::ApplyHygienePass;
using ned::editor::EnsureFinalNewline;
using ned::editor::MaxConsecutiveBlankLines;
using ned::editor::SetEnsureFinalNewline;
using ned::editor::SetMaxConsecutiveBlankLines;
using ned::editor::SetTrimTrailingWhitespaceOnSave;
using ned::editor::TrimTrailingWhitespaceOnSave;
using ned::text::Buffer;

namespace {

// Mirrors TrimOnSaveTest.cpp/FinalNewlineTest.cpp/MaxConsecutiveBlankLinesTest.cpp's
// own guards -- all three are process-wide state.
struct HygieneSettingsGuard {
    ~HygieneSettingsGuard() {
        SetTrimTrailingWhitespaceOnSave(true);
        SetEnsureFinalNewline(true);
        SetMaxConsecutiveBlankLines(2);
    }
};

} // namespace

TEST_CASE("ApplyHygienePass trims, collapses blank runs, and ensures a final newline, as one undo step",
          "[Format]") {
    const HygieneSettingsGuard guard;
    Buffer                     buffer("test.txt");
    buffer.InsertAtPoint("a   \nb\n\n\n\nc"); // "b" then 4 newlines (3 blank lines) then "c", no trailing newline

    REQUIRE(ApplyHygienePass(buffer));
    // trim: "a   " -> "a" (the 3 mid-document blank lines aren't at EOF, so
    // trim's own EOF-collapse doesn't touch them); collapse: 3 blank lines
    // -> 2 (the default cap); final newline: appended, since the input had
    // none.
    REQUIRE(buffer.Text() == "a\nb\n\n\nc\n");

    REQUIRE(buffer.CanUndo());
    buffer.Undo();
    REQUIRE(buffer.Text() == "a   \nb\n\n\n\nc"); // the whole pass undoes as one step
}

TEST_CASE("ApplyHygienePass is a no-op on already-clean content", "[Format]") {
    const HygieneSettingsGuard guard;
    Buffer                     buffer("test.txt");
    buffer.InsertAtPoint("a\nb\nc\n");
    const bool couldUndoBefore = buffer.CanUndo();

    REQUIRE_FALSE(ApplyHygienePass(buffer));
    REQUIRE(buffer.Text() == "a\nb\nc\n");
    REQUIRE(buffer.CanUndo() == couldUndoBefore); // no new undo entry was pushed
}

TEST_CASE("ApplyHygienePass skips trimming when TrimTrailingWhitespaceOnSave is disabled", "[Format]") {
    const HygieneSettingsGuard guard;
    SetTrimTrailingWhitespaceOnSave(false);
    Buffer buffer("test.txt");
    buffer.InsertAtPoint("a   \nb\n");

    ApplyHygienePass(buffer);
    REQUIRE(buffer.Text() == "a   \nb\n"); // trailing spaces untouched
}

TEST_CASE("ApplyHygienePass skips the final newline when EnsureFinalNewline is disabled", "[Format]") {
    const HygieneSettingsGuard guard;
    SetEnsureFinalNewline(false);
    SetTrimTrailingWhitespaceOnSave(false); // isolate: don't let trim's own EOF-collapse interfere
    Buffer buffer("test.txt");
    buffer.InsertAtPoint("a");

    ApplyHygienePass(buffer);
    REQUIRE(buffer.Text() == "a");
}

TEST_CASE("ApplyHygienePass skips blank-line collapsing when the limit is disabled", "[Format]") {
    const HygieneSettingsGuard guard;
    SetMaxConsecutiveBlankLines(std::nullopt);
    SetTrimTrailingWhitespaceOnSave(false); // isolate: trim's own EOF-collapse would otherwise remove these too
    Buffer buffer("test.txt");
    buffer.InsertAtPoint("a\n\n\n\n\nb\n");

    ApplyHygienePass(buffer);
    REQUIRE(buffer.Text() == "a\n\n\n\n\nb\n");
}
