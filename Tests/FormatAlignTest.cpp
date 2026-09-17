#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatAlign.h"
#include "Editor/FormatEdit.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::AlignRuleFor;
using ned::editor::ApplyFormatTextEdits;
using ned::editor::ComputeAlignEdits;
using ned::editor::CppMode;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::Mode;
using ned::editor::SetAlignEnabled;
using ned::text::Buffer;

namespace {

// align-kind follow-up: the same test-isolation lesson FormatWrapTest.cpp's
// own FormatRulesGuard already learned once -- a manual reset at the end of
// a test body never runs if an earlier REQUIRE in that body fails first.
struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetAlignEnabled("align.assignment", std::nullopt);
        SetAlignEnabled("cpp/align.assignment", std::nullopt);
        SetAlignEnabled("align.enumerator", std::nullopt);
    }
};

std::vector<FormatCapture> CapturesNamed(const std::vector<FormatCapture>& captures, std::string_view name) {
    std::vector<FormatCapture> result;
    for (const FormatCapture& c : captures) {
        if (c.name == name) {
            result.push_back(c);
        }
    }
    return result;
}

} // namespace

TEST_CASE("cpp-mode's format.janet names align.assignment on a plain reassignment's own operator",
          "[FormatAlign]") {
    const Mode mode     = CppMode();
    const auto captures = CapturesNamed(mode.formatCaptures("x = 1;\n"), "align.assignment");
    REQUIRE(captures.size() == 1);
    REQUIRE(captures[0].startByte == 2);
    REQUIRE(captures[0].endByte == 3);
}

TEST_CASE("ComputeAlignEdits does nothing when unconfigured", "[FormatAlign]") {
    const Mode        mode   = CppMode();
    const std::string source = "x = 1;\nyy = 2;\n";
    REQUIRE(ComputeAlignEdits(source, "cpp", mode.formatCaptures(source)).empty());
}

TEST_CASE("End to end: a run of adjacent same-indent assignments gets its operators padded to a "
          "shared column, the longest line left untouched",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.assignment", true);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("x = 1;\nyy = 2;\n");
    ApplyFormatTextEdits(buffer, ComputeAlignEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "x  = 1;\nyy = 2;\n");
}

TEST_CASE("End to end: a zero-space anchor still gets padded to the run's own target column",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.assignment", true);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("a=1;\nbb = 2;\n");
    ApplyFormatTextEdits(buffer, ComputeAlignEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "a  =1;\nbb = 2;\n");
}

TEST_CASE("End to end: idempotent -- re-running against already-aligned output changes nothing",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.assignment", true);

    const Mode        mode   = CppMode();
    const std::string source = "x  = 1;\nyy = 2;\n";
    Buffer            buffer("t.cpp");
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, ComputeAlignEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == source);
}

TEST_CASE("End to end: the run's own widest member is left untouched even when its own anchor "
          "already touches the preceding token with no gap at all",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.assignment", true);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("ccc=3;\na = 1;\n");
    ApplyFormatTextEdits(buffer, ComputeAlignEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "ccc=3;\na  = 1;\n");
}

TEST_CASE("A blank line between two assignments breaks the run -- neither gets touched",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.assignment", true);

    const Mode        mode   = CppMode();
    const std::string source = "x = 1;\n\nyy = 2;\n";
    REQUIRE(ComputeAlignEdits(source, "cpp", mode.formatCaptures(source)).empty());
}

TEST_CASE("Two assignments at different indent depths never join one run", "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.assignment", true);

    const Mode        mode   = CppMode();
    const std::string source = "x = 1;\n  yy = 2;\n";
    REQUIRE(ComputeAlignEdits(source, "cpp", mode.formatCaptures(source)).empty());
}

TEST_CASE("A run of three lines aligns every operator to the widest name's own column",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.assignment", true);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("a = 1;\nbb = 2;\nccc = 3;\n");
    ApplyFormatTextEdits(buffer, ComputeAlignEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "a   = 1;\nbb  = 2;\nccc = 3;\n");
}

// align-kind widening: a second construct, cpp's own enum-member
// initializer -- see cpp/format.janet's own header comment for
// align.enumerator. A bare enumerator with no initializer produces no
// match at all, for free (its "value" field is optional in the grammar).
TEST_CASE("cpp-mode's format.janet names align.enumerator on an enumerator's own \"=\", and "
          "not at all on a bare enumerator with no initializer",
          "[FormatAlign]") {
    const Mode mode = CppMode();

    const auto withValue = CapturesNamed(mode.formatCaptures("enum E { Red = 1 };\n"), "align.enumerator");
    REQUIRE(withValue.size() == 1);

    REQUIRE(CapturesNamed(mode.formatCaptures("enum E { Red };\n"), "align.enumerator").empty());
}

TEST_CASE("End to end: a run of adjacent enum-member initializers gets padded to a shared "
          "column",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.enumerator", true);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("enum Color {\n    Red = 1,\n    Green = 2,\n};\n");
    ApplyFormatTextEdits(buffer, ComputeAlignEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "enum Color {\n    Red   = 1,\n    Green = 2,\n};\n");
}

TEST_CASE("End to end: an uninitialized enumerator inside the run is simply not part of it -- "
          "the remaining initialized members still align to each other",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.enumerator", true);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("enum Color {\n    Red = 1,\n    Blue,\n    Green = 2,\n};\n");
    ApplyFormatTextEdits(buffer, ComputeAlignEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    // Red/Green are not adjacent lines of the SAME capture once Blue's own
    // uncaptured line sits between them -- each is its own run of size one,
    // so neither gets touched.
    REQUIRE(buffer.Text() == "enum Color {\n    Red = 1,\n    Blue,\n    Green = 2,\n};\n");
}

TEST_CASE("AlignRuleFor(name, language) resolves the language-scoped key first, matching every "
          "other rule kind's own precedent",
          "[FormatAlign]") {
    const FormatRulesGuard guard;
    SetAlignEnabled("align.assignment", true);
    SetAlignEnabled("cpp/align.assignment", false);

    REQUIRE(AlignRuleFor("align.assignment", "cpp").enabled == false);
    REQUIRE(AlignRuleFor("align.assignment", "python").enabled == true); // falls through
}
