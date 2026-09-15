#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatRules.h"
#include "Editor/FormatSpacing.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::CppMode;
using ned::editor::ComputeSpaceEdits;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::Mode;
using ned::editor::SetSpaceAfter;
using ned::editor::SetSpaceBefore;
using ned::editor::SetSpaceWithin;
using ned::text::Buffer;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetSpaceBefore("control.parens", std::nullopt);
        SetSpaceAfter("control.parens", std::nullopt);
        SetSpaceWithin("control.parens", std::nullopt);
        SetSpaceBefore("cpp/control.parens", std::nullopt);
    }
};

// Finds the FormatCapture named "control.parens" in a hand-built list --
// every test below only ever has one.
FormatCapture ParensCapture(std::string_view text) {
    return {"control.parens", text.find('('), text.find(')') + 1};
}

} // namespace

TEST_CASE("cpp-mode's format.janet names control.parens over a real if statement", "[FormatSpacing]") {
    const Mode mode = CppMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "if(x){\n}\n";
    const std::vector<FormatCapture> captures = mode.formatCaptures(source);

    REQUIRE(captures.size() == 1);
    REQUIRE(captures[0].name == "control.parens");
    REQUIRE(captures[0].startByte == source.find('('));
    REQUIRE(captures[0].endByte == source.find(')') + 1);
}

TEST_CASE("cpp-mode's format.janet also captures a while statement's condition", "[FormatSpacing]") {
    const Mode mode = CppMode();
    const std::string source = "while(x){\n}\n";
    const std::vector<FormatCapture> captures = mode.formatCaptures(source);

    REQUIRE(captures.size() == 1);
    REQUIRE(captures[0].name == "control.parens");
}

TEST_CASE("An unconfigured capture contributes no edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    const std::string text = "if(x){\n}\n";
    REQUIRE(ComputeSpaceEdits(text, "cpp", {ParensCapture(text)}).empty());
}

TEST_CASE(":before inserts a missing space and removes an unwanted one", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    {
        const std::string text = "if(x) {}";
        const std::vector<FormatTextEdit> edits = ComputeSpaceEdits(text, "cpp", {ParensCapture(text)});
        REQUIRE(edits.size() == 1);
        REQUIRE(edits[0].start == text.find('('));
        REQUIRE(edits[0].end == text.find('('));
        REQUIRE(edits[0].text == " ");
    }

    SetSpaceBefore("control.parens", false);
    {
        const std::string text = "if  (x) {}"; // two spaces before '('
        const std::vector<FormatTextEdit> edits = ComputeSpaceEdits(text, "cpp", {ParensCapture(text)});
        REQUIRE(edits.size() == 1);
        REQUIRE(edits[0].text.empty());
        REQUIRE(text.substr(edits[0].start, edits[0].end - edits[0].start) == "  ");
    }
}

TEST_CASE(":after governs the gap right after the closing paren", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceAfter("control.parens", true);

    const std::string text = "if(x){}";
    const std::vector<FormatTextEdit> edits = ComputeSpaceEdits(text, "cpp", {ParensCapture(text)});
    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].start == text.find(')') + 1);
    REQUIRE(edits[0].text == " ");
}

TEST_CASE(":within adjusts both the just-inside-open and just-inside-close gaps", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceWithin("control.parens", true);

    const std::string text = "if(x){}";
    const std::vector<FormatTextEdit> edits = ComputeSpaceEdits(text, "cpp", {ParensCapture(text)});
    REQUIRE(edits.size() == 2);
    REQUIRE(edits[0].text == " "); // just inside '('
    REQUIRE(edits[1].text == " "); // just inside ')'
}

TEST_CASE("A gap crossing a newline is never touched", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const std::string text = "if\n(x) {}"; // 'if' and '(' on different lines
    REQUIRE(ComputeSpaceEdits(text, "cpp", {ParensCapture(text)}).empty());
}

TEST_CASE("A language-scoped rule wins over the shared one", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);
    SetSpaceBefore("cpp/control.parens", false);

    const std::string text = "if(x) {}";
    REQUIRE(ComputeSpaceEdits(text, "cpp", {ParensCapture(text)}).empty()); // already 0 spaces, cpp wants false
    REQUIRE(ComputeSpaceEdits(text, "python", {ParensCapture(text)}).size() == 1); // falls through to true
}

TEST_CASE("Recomputing against the edited result is idempotent", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);
    SetSpaceWithin("control.parens", true);

    Buffer buffer("test.cpp");
    buffer.InsertAtPoint("if(x) {}");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "cpp", {ParensCapture(buffer.Text())}));
    REQUIRE(buffer.Text() == "if ( x ) {}");

    REQUIRE(ComputeSpaceEdits(buffer.Text(), "cpp", {ParensCapture(buffer.Text())}).empty());
}

TEST_CASE("End to end: a real cpp Mode's formatCaptures drives a real edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);
    SetSpaceAfter("control.parens", true);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("if(x){\n    return;\n}\n");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "if (x) {\n    return;\n}\n");
}
