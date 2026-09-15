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
using ned::editor::JavaMode;
using ned::editor::JavaScriptMode;
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

// Real format.janet files now name more than one capture over the same
// source (a control statement's own condition parens AND its own brace
// body, see cpp/javascript's format.janet's "capture-coverage-widening"
// comments) -- filter by name rather than asserting a total count, so this
// file doesn't need touching every time coverage widens further.
std::vector<FormatCapture> CapturesNamed(const std::vector<FormatCapture>& captures, std::string_view name) {
    std::vector<FormatCapture> matches;
    for (const FormatCapture& capture : captures) {
        if (capture.name == name) {
            matches.push_back(capture);
        }
    }
    return matches;
}

} // namespace

TEST_CASE("cpp-mode's format.janet names control.parens over a real if statement", "[FormatSpacing]") {
    const Mode mode = CppMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "if(x){\n}\n";
    const std::vector<FormatCapture> parens = CapturesNamed(mode.formatCaptures(source), "control.parens");

    REQUIRE(parens.size() == 1);
    REQUIRE(parens[0].startByte == source.find('('));
    REQUIRE(parens[0].endByte == source.find(')') + 1);
}

TEST_CASE("cpp-mode's format.janet also captures a while statement's condition", "[FormatSpacing]") {
    const Mode mode = CppMode();
    const std::string source = "while(x){\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 1);
}

TEST_CASE("javascript-mode's format.janet also names control.parens, over a different node type", "[FormatSpacing]") {
    const Mode mode = JavaScriptMode();
    const std::string source = "if(x){\n}\n";
    // same capture name as cpp's, different grammar node underneath
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 1);
}

TEST_CASE("cpp-mode's format.janet names control.parens for switch and catch too", "[FormatSpacing]") {
    const Mode mode = CppMode();

    const std::string switchSource = "switch(x){\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(switchSource), "control.parens").size() == 1);

    const std::string tryCatchSource = "try {\n} catch(int e) {\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(tryCatchSource), "control.parens").size() == 1);
}

TEST_CASE("javascript-mode's format.janet names control.parens for switch and catch too", "[FormatSpacing]") {
    const Mode mode = JavaScriptMode();

    const std::string switchSource = "switch(x){\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(switchSource), "control.parens").size() == 1);

    // Captured via the paired-delimiter-captures mechanism -- javascript's
    // catch_clause has no wrapping parens node the way cpp's parameter_list
    // is, only bare "(" ")" anonymous tokens (see the format.janet's own
    // comment).
    const std::string tryCatchSource = "try {\n} catch(e) {\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(tryCatchSource), "control.parens").size() == 1);

    // ES2019+'s parameter-less catch has no "(" ")" at all -- the paired
    // pattern simply doesn't match, not a false capture.
    const std::string catchNoParamSource = "try {\n} catch {\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(catchNoParamSource), "control.parens").empty());
}

// paired-delimiter-captures follow-up: a for-loop's own
// "(init; condition; update)" has no single node spanning the whole clause
// in either grammar -- captured as a matched "<name>.open"/"<name>.close"
// pair of single anonymous tokens instead (Mode.cpp's formatCaptures
// closure correlates them per pattern match). These tests exercise that
// mechanism specifically, not just the whole-span capture shape every
// other test above uses.
TEST_CASE("cpp-mode's format.janet captures a for-loop's own parens as a matched pair", "[FormatSpacing]") {
    const Mode mode = CppMode();
    // "void f()"'s own empty parameter-list parens come first in the source
    // -- deliberately, so a naive find('(') would grab the WRONG pair and
    // this test would pass for the wrong reason.
    const std::string source = "void f() { for (int i = 0; i < 10; ++i) {} }";
    const std::vector<FormatCapture> parens = CapturesNamed(mode.formatCaptures(source), "control.parens");

    REQUIRE(parens.size() == 1);
    REQUIRE(source.substr(parens[0].startByte, parens[0].endByte - parens[0].startByte) ==
            "(int i = 0; i < 10; ++i)");
}

TEST_CASE("A nested call's own parens inside a for-loop condition don't confuse the pair", "[FormatSpacing]") {
    const Mode mode = CppMode();
    const std::string source = "void g() { for (int i = 0; i < f(x); ++i) {} }";
    const std::vector<FormatCapture> parens = CapturesNamed(mode.formatCaptures(source), "control.parens");

    REQUIRE(parens.size() == 1); // not 2 -- f(x)'s own parens are a nested descendant, never a direct child
    REQUIRE(source.substr(parens[0].startByte, parens[0].endByte - parens[0].startByte) ==
            "(int i = 0; i < f(x); ++i)");
}

TEST_CASE("Two adjacent for-loops each get their own pair, never cross-paired", "[FormatSpacing]") {
    const Mode mode = CppMode();
    const std::string source = "void f() { for (int i = 0; i < 1; ++i) {} for (int j = 0; j < 2; ++j) {} }";
    const std::vector<FormatCapture> parens = CapturesNamed(mode.formatCaptures(source), "control.parens");

    REQUIRE(parens.size() == 2);
    REQUIRE(source.substr(parens[0].startByte, parens[0].endByte - parens[0].startByte) == "(int i = 0; i < 1; ++i)");
    REQUIRE(source.substr(parens[1].startByte, parens[1].endByte - parens[1].startByte) == "(int j = 0; j < 2; ++j)");
}

TEST_CASE("javascript-mode's format.janet captures a for-loop's own parens as a matched pair too", "[FormatSpacing]") {
    const Mode mode = JavaScriptMode();
    const std::string source = "function f() { for (let i = 0; i < 10; ++i) {} }";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 1);
}

TEST_CASE(":before applies correctly to a paired for-loop capture", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("void f() { for(int i = 0; i < 10; ++i) {} }");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "void f() { for (int i = 0; i < 10; ++i) {} }");
}

TEST_CASE("A per-language override actually differentiates two real languages", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);       // the shared rule
    SetSpaceBefore("cpp/control.parens", false);  // cpp's own exception

    const std::string cppSource = "if(x) {}";
    const std::string jsSource  = "if(x) {}";

    REQUIRE(ComputeSpaceEdits(cppSource, "cpp", CppMode().formatCaptures(cppSource)).empty()); // cpp wants none, already none
    REQUIRE(ComputeSpaceEdits(jsSource, "javascript", JavaScriptMode().formatCaptures(jsSource)).size() == 1); // js falls through to the shared rule
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

// Regression: found live 2026-09-15 alongside the NextLineIndented bug
// below, when the user asked whether anything else had been "declared"
// rather than verified. A genuinely empty pair has the "just inside open"
// and "just inside close" gaps sitting at the exact same byte position --
// emitting both independently double-inserted ("(  )" instead of "( )").
TEST_CASE(":within on a genuinely empty pair inserts exactly one space, not two", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceWithin("control.parens", true);

    const std::string source = "if() {}";
    const std::vector<FormatCapture> captures = {{"control.parens", source.find('('), source.find(')') + 1}};
    const std::vector<FormatTextEdit> edits = ComputeSpaceEdits(source, "cpp", captures);
    REQUIRE(edits.size() == 1);

    Buffer buffer("test.cpp");
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, edits);
    REQUIRE(buffer.Text() == "if( ) {}");

    // Idempotent: re-running against the result finds nothing left to do.
    const std::vector<FormatCapture> after = {{"control.parens", buffer.Text().find('('), buffer.Text().find(')') + 1}};
    REQUIRE(ComputeSpaceEdits(buffer.Text(), "cpp", after).empty());
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

// java-mode: the third language over the full template.
TEST_CASE("java-mode's format.janet names control.parens over if/switch", "[FormatSpacing]") {
    const Mode mode = JavaMode();
    const std::string ifSource     = "class C { void m() { if(x) {} } }";
    const std::string switchSource = "class C { void m() { switch(x) {} } }";
    REQUIRE(CapturesNamed(mode.formatCaptures(ifSource), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures(switchSource), "control.parens").size() == 1);
}

TEST_CASE("java-mode's format.janet captures a for-loop's outer parens despite multiple init/update expressions",
          "[FormatSpacing]") {
    const Mode mode = JavaMode();
    const std::string source = "class C { void m() { for (int i = 0, j = 1; i < 10; ++i, --j) {} } }";
    const auto        parens = CapturesNamed(mode.formatCaptures(source), "control.parens");

    REQUIRE(parens.size() == 1); // one pair, not one per comma-separated expression
    REQUIRE(source.substr(parens[0].startByte, parens[0].endByte - parens[0].startByte) ==
            "(int i = 0, j = 1; i < 10; ++i, --j)");
}

TEST_CASE("java-mode's format.janet captures a catch clause's parens as a matched pair", "[FormatSpacing]") {
    const Mode mode = JavaMode();
    const std::string source = "class C { void m() { try {} catch (Exception e) {} } }";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 1);
}

TEST_CASE("End to end: java-mode's formatCaptures drives a real space edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = JavaMode();
    Buffer     buffer("test.java");
    buffer.InsertAtPoint("class C { void m() { if(x) {\n    return;\n} } }");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "java", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "class C { void m() { if (x) {\n    return;\n} } }");
}
