#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatEdit.h"
#include "Editor/FormatRules.h"
#include "Editor/FormatWrap.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::ComputeWrapEdits;
using ned::editor::CppMode;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::Mode;
using ned::editor::SetWrapForceTrailingComma;
using ned::editor::SetWrapPolicy;
using ned::editor::WrapPolicy;
using ned::text::Buffer;

namespace {

// wrap-kind follow-up: the SAME test-isolation lesson this rollout's own
// FormatBracePlacementTest.cpp guard already learned once (a manual reset
// at the end of a test body never runs if an earlier REQUIRE in that same
// body fails first) -- built with that lesson from day one rather than
// re-discovering it.
struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetWrapPolicy("wrap.args", std::nullopt);
        SetWrapForceTrailingComma("wrap.args", std::nullopt);
        SetWrapPolicy("cpp/wrap.args", std::nullopt);
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

// cpp-mode: the pilot construct for the whole rule kind -- see
// cpp/format.janet's own header comment for the full reasoning.
TEST_CASE("cpp-mode's format.janet names wrap.args with correct item spans, over zero/one/"
          "many arguments and nested calls",
          "[FormatWrap]") {
    const Mode mode = CppMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("int a = f();"), "wrap.args")[0].items.empty());

    const auto one = CapturesNamed(mode.formatCaptures("int b = f(1);"), "wrap.args");
    REQUIRE(one.size() == 1);
    REQUIRE(one[0].items.size() == 1);

    const auto three = CapturesNamed(mode.formatCaptures("int c = f(1, 2, 3);"), "wrap.args");
    REQUIRE(three.size() == 1);
    REQUIRE(three[0].items.size() == 3);

    // A nested call gets its own independent capture -- the outer list's
    // own items are ITS direct children only (the whole nested call is one
    // item, not reached into), confirmed live before this was written.
    const std::string  nested       = "int d = f(g(1, 2), 3);";
    const auto         nestedCaptures = CapturesNamed(mode.formatCaptures(nested), "wrap.args");
    REQUIRE(nestedCaptures.size() == 2);
    const bool hasOuter = nestedCaptures[0].items.size() == 2 || nestedCaptures[1].items.size() == 2;
    const bool hasInner = nestedCaptures[0].items.size() == 2 && nestedCaptures[1].items.size() == 2;
    REQUIRE(hasOuter);
    REQUIRE(hasInner); // both the outer (g(1,2), 3) and inner (1, 2) calls have exactly 2 items
}

TEST_CASE("ComputeWrapEdits does nothing when no policy is configured", "[FormatWrap]") {
    const Mode        mode   = CppMode();
    const std::string source = "int c = f(1,\n    2,\n  3);\n";
    REQUIRE(ComputeWrapEdits(source, "cpp", mode.formatCaptures(source)).empty());
}

TEST_CASE("End to end: WrapPolicy::Never collapses a multi-line argument list onto one line",
          "[FormatWrap]") {
    const FormatRulesGuard guard;
    SetWrapPolicy("wrap.args", WrapPolicy::Never);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("int c = f(1,\n    2,\n  3);\n");
    ApplyFormatTextEdits(buffer, ComputeWrapEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int c = f(1, 2, 3);\n");
}

TEST_CASE("End to end: WrapPolicy::Always chops an argument list to one item per line, "
          "closer aligned with the header",
          "[FormatWrap]") {
    const FormatRulesGuard guard;
    SetWrapPolicy("wrap.args", WrapPolicy::Always);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("int c = f(1, 2, 3);\n");
    ApplyFormatTextEdits(buffer, ComputeWrapEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int c = f(\n    1,\n    2,\n    3\n);\n");
}

TEST_CASE("End to end: WrapPolicy::Always respects the header's own indent, not column zero",
          "[FormatWrap]") {
    const FormatRulesGuard guard;
    SetWrapPolicy("wrap.args", WrapPolicy::Always);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("void g() {\n  int c = f(1, 2, 3);\n}\n");
    ApplyFormatTextEdits(buffer, ComputeWrapEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "void g() {\n  int c = f(\n      1,\n      2,\n      3\n  );\n}\n");
}

TEST_CASE("End to end: WrapPolicy::Always is idempotent -- re-running against already-"
          "chopped output changes nothing",
          "[FormatWrap]") {
    const FormatRulesGuard guard;
    SetWrapPolicy("wrap.args", WrapPolicy::Always);

    const Mode        mode   = CppMode();
    const std::string source = "int c = f(\n    1,\n    2,\n    3\n);\n";
    Buffer            buffer("t.cpp");
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, ComputeWrapEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == source);
}

// A real corruption hazard found live, not by inspection -- see
// cpp/format.janet's own header comment and FormatWrap.cpp's own
// TrailingCommaUnsafeForLanguage for the full reasoning. Confirmed with a
// real `g++` compile: WITHOUT this guard, the trailing comma this test
// configures produces "expected primary-expression before ')' token".
TEST_CASE("End to end: :force-trailing-comma is declined for cpp's own wrap.args (a call's "
          "own trailing comma is a hard C++ syntax error, confirmed live)",
          "[FormatWrap]") {
    const FormatRulesGuard guard;
    SetWrapPolicy("wrap.args", WrapPolicy::Always);
    SetWrapForceTrailingComma("wrap.args", true);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("int c = f(1, 2, 3);\n");
    ApplyFormatTextEdits(buffer, ComputeWrapEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    // NOT "...3,\n);\n" -- confirmed live that misparses.
    REQUIRE(buffer.Text() == "int c = f(\n    1,\n    2,\n    3\n);\n");
}

TEST_CASE("End to end: WrapPolicy::Always leaves a zero-argument call untouched, and chops "
          "a one-argument call the same as a many-argument one",
          "[FormatWrap]") {
    const FormatRulesGuard guard;
    SetWrapPolicy("wrap.args", WrapPolicy::Always);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("int a = f();\nint b = f(1);\n");
    ApplyFormatTextEdits(buffer, ComputeWrapEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int a = f();\nint b = f(\n    1\n);\n");
}

TEST_CASE("WrapRuleFor(name, language) resolves the language-scoped key first, matching "
          "every other rule kind's own precedent",
          "[FormatWrap]") {
    const FormatRulesGuard guard;
    SetWrapPolicy("wrap.args", WrapPolicy::Always);
    SetWrapPolicy("cpp/wrap.args", WrapPolicy::Never);

    REQUIRE(ned::editor::WrapRuleFor("wrap.args", "cpp").policy == WrapPolicy::Never);
    REQUIRE(ned::editor::WrapRuleFor("wrap.args", "python").policy == WrapPolicy::Always); // falls through
}
