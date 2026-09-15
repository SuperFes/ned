#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatRules.h"
#include "Editor/FormatSpacing.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::BashMode;
using ned::editor::CMode;
using ned::editor::ComputeSpaceEdits;
using ned::editor::CppMode;
using ned::editor::CSharpMode;
using ned::editor::FishMode;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::GoMode;
using ned::editor::JavaMode;
using ned::editor::JavaScriptMode;
using ned::editor::KotlinMode;
using ned::editor::LuaMode;
using ned::editor::Mode;
using ned::editor::PhpMode;
using ned::editor::PythonMode;
using ned::editor::RustMode;
using ned::editor::SetSpaceAfter;
using ned::editor::SetSpaceBefore;
using ned::editor::SetSpaceWithin;
using ned::editor::TypeScriptMode;
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

// python-mode: the language whose grammar has no brace-delimited bodies at
// all (function/if/while/for/class/try/with/match all use a bare "block"
// field, verified against tree-sitter-python's node-types.json) -- its
// format.janet names no brace.*/collapse-*/paired captures at all, only
// control.parens, and only for the rare case a condition is already
// parenthesized (the grammar never requires it).
TEST_CASE("python-mode's format.janet only captures a condition already wrapped in parens", "[FormatSpacing]") {
    const Mode mode = PythonMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("if x:\n    pass\n"), "control.parens").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("if (x):\n    pass\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("while x:\n    pass\n"), "control.parens").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("while (x and y):\n    pass\n"), "control.parens").size() == 1);
}

TEST_CASE("python-mode's format.janet has no brace-shaped captures at all", "[FormatSpacing]") {
    const Mode        mode   = PythonMode();
    const std::string source = "def f(x):\n"
                                "    if x:\n"
                                "        pass\n"
                                "    while x:\n"
                                "        pass\n"
                                "class C:\n"
                                "    def m(self):\n"
                                "        pass\n";
    for (const char* name : {"brace.function", "brace.control", "brace.class"}) {
        REQUIRE(CapturesNamed(mode.formatCaptures(source), name).empty());
    }
}

TEST_CASE("End to end: python-mode's formatCaptures drives a real space edit only when parens are present",
          "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceWithin("control.parens", false);

    const Mode mode = PythonMode();

    Buffer withParens("test.py");
    withParens.InsertAtPoint("if ( x ):\n    pass\n");
    ApplyFormatTextEdits(withParens,
                          ComputeSpaceEdits(withParens.Text(), "python", mode.formatCaptures(withParens.Text())));
    REQUIRE(withParens.Text() == "if (x):\n    pass\n");

    Buffer withoutParens("test2.py");
    withoutParens.InsertAtPoint("if x:\n    pass\n");
    ApplyFormatTextEdits(
        withoutParens, ComputeSpaceEdits(withoutParens.Text(), "python", mode.formatCaptures(withoutParens.Text())));
    REQUIRE(withoutParens.Text() == "if x:\n    pass\n"); // nothing to touch, no crash either
}

// go-mode: the fourth brace-carrying language, same narrow control.parens
// lever Python's own file has -- Go's idiomatic style omits condition
// parens entirely, but the grammar still allows writing them.
TEST_CASE("go-mode's format.janet only captures a condition already wrapped in parens", "[FormatSpacing]") {
    const Mode mode = GoMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("package main\nfunc f() {\n\tif x {\n\t}\n}\n"), "control.parens")
                .empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("package main\nfunc f() {\n\tif (x) {\n\t}\n}\n"), "control.parens")
                .size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("package main\nfunc f() {\n\tswitch x {\n\t}\n}\n"), "control.parens")
                .empty());
    REQUIRE(
        CapturesNamed(mode.formatCaptures("package main\nfunc f() {\n\tswitch (x) {\n\t}\n}\n"), "control.parens")
            .size() == 1);
}

TEST_CASE("go-mode's format.janet names no control.parens for a for-loop's own clause", "[FormatSpacing]") {
    // A for-loop's three-part clause has no wrapping parens in Go's own
    // grammar at all -- writing them is a syntax error, unlike
    // cpp/javascript/java's own for-loops, so there is no paired capture
    // for it here (verified live before this shipped).
    const Mode        mode   = GoMode();
    const std::string source = "package main\nfunc f() {\n\tfor i := 0; i < 10; i++ {\n\t}\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").empty());
}

TEST_CASE("End to end: go-mode's formatCaptures drives a real space edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = GoMode();
    Buffer     buffer("test.go");
    buffer.InsertAtPoint("package main\nfunc f() {\n\tif(x) {\n\t}\n}\n");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "go", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "package main\nfunc f() {\n\tif (x) {\n\t}\n}\n");
}

// php-mode: the sixth language. Unlike Python/Go's own narrow,
// rarely-matched control.parens lever, PHP's if/while/switch condition is
// a REQUIRED parenthesized_expression -- ordinary parens, not a redundant
// edge case, matching cpp/javascript/java's own shape.
TEST_CASE("php-mode's format.janet names control.parens over if/while/switch", "[FormatSpacing]") {
    const Mode mode = PhpMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("<?php\nif ($x) {\n}\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("<?php\nwhile ($x) {\n}\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("<?php\nswitch ($x) {\n}\n"), "control.parens").size() == 1);
}

TEST_CASE("php-mode's format.janet names control.parens over elseif's own condition too", "[FormatSpacing]") {
    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nif ($x) {\n} elseif ($y) {\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 2);
}

TEST_CASE("php-mode's format.janet captures a for-loop's outer parens as a matched pair", "[FormatSpacing]") {
    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nfor ($i = 0; $i < 10; $i++) {\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 1);
}

TEST_CASE("php-mode's format.janet captures a catch clause's parens as a matched pair", "[FormatSpacing]") {
    const Mode        mode   = PhpMode();
    const std::string source = "<?php\ntry {\n} catch (Exception $e) {\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 1);
}

TEST_CASE("End to end: php-mode's formatCaptures drives a real space edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = PhpMode();
    Buffer     buffer("test.php");
    buffer.InsertAtPoint("<?php\nif($x) {\n    return;\n}\n");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "php", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "<?php\nif ($x) {\n    return;\n}\n");
}

// rust-mode: the seventh language. Like go/python, idiomatic Rust omits
// parens on if/while/match entirely, but the grammar still allows them
// (parenthesized_expression is one of `_expression`'s own subtypes), so
// this is the same narrow lever -- only fires when parens are actually
// present, verified live.
TEST_CASE("rust-mode's format.janet only captures a condition already wrapped in parens", "[FormatSpacing]") {
    const Mode mode = RustMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("fn f() {\n    if x {\n    }\n}\n"), "control.parens").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("fn f() {\n    if (x) {\n    }\n}\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("fn f() {\n    match x {\n    }\n}\n"), "control.parens").empty());
    REQUIRE(
        CapturesNamed(mode.formatCaptures("fn f() {\n    match (x) {\n    }\n}\n"), "control.parens").size() == 1);
}

TEST_CASE("rust-mode's format.janet names no control.parens for a for-loop's own iterable",
          "[FormatSpacing]") {
    // Unlike match's own "value:", a for-loop's iterable is deliberately
    // declined -- not a grammar limitation (wrapping it in parens parses
    // fine, verified live), a scope choice: it's not a scrutinee/condition
    // the way if/while/match's own value is. See format.janet's own
    // comment.
    const Mode mode = RustMode();
    REQUIRE(
        CapturesNamed(mode.formatCaptures("fn f() {\n    for x in (0..3) {\n    }\n}\n"), "control.parens")
            .empty());
}

TEST_CASE("End to end: rust-mode's formatCaptures drives a real space edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = RustMode();
    Buffer     buffer("test.rs");
    buffer.InsertAtPoint("fn f() {\n    if(x) {\n    }\n}\n");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "rust", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "fn f() {\n    if (x) {\n    }\n}\n");
}

// csharp-mode: the eighth language, and the one where "mandatory parens"
// still needed the PAIRED mechanism -- unlike cpp/java/javascript's own
// condition field (a node spanning the whole "(...)"), c#'s is a bare
// expression with the parens as unwrapped anonymous tokens (confirmed via
// grammar.json).
TEST_CASE("csharp-mode's format.janet names control.parens over if/while/switch, as a matched pair",
          "[FormatSpacing]") {
    const Mode mode = CSharpMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void M() { if (x) {} } }"), "control.parens").size()
            == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void M() { while (x) {} } }"), "control.parens").size()
            == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void M() { switch (x) {} } }"), "control.parens").size()
            == 1);
}

TEST_CASE("csharp-mode's format.janet captures a for/foreach loop's own parens as a matched pair too",
          "[FormatSpacing]") {
    const Mode mode = CSharpMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void M() { for (int i = 0; i < 10; i++) {} } }"),
                          "control.parens")
                .size()
            == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void M() { foreach (var i in xs) {} } }"), "control.parens")
                .size()
            == 1);
}

TEST_CASE("csharp-mode's format.janet captures a catch clause's own parens directly, not paired",
          "[FormatSpacing]") {
    // catch_declaration's own node span really does cover "(Type name)"
    // whole, confirmed via grammar.json's rule (its own STRING "(" is the
    // rule's first member) rather than assumed from its field list, which
    // only names "type"/"name" and says nothing about the node's own byte
    // span.
    const Mode        mode   = CSharpMode();
    const std::string source = "class C { void M() { try {} catch (Exception e) {} } }";
    const auto        caps   = CapturesNamed(mode.formatCaptures(source), "control.parens");
    REQUIRE(caps.size() == 1);
    REQUIRE(caps[0].startByte == source.find('(', source.find("catch")));
    REQUIRE(caps[0].endByte == source.find(')', source.find("catch")) + 1);
}

TEST_CASE("End to end: csharp-mode's formatCaptures drives a real space edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = CSharpMode();
    Buffer     buffer("test.cs");
    buffer.InsertAtPoint("class C {\n    void M() {\n        if(x) {\n        }\n    }\n}\n");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "csharp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "class C {\n    void M() {\n        if (x) {\n        }\n    }\n}\n");
}

// typescript-mode: control.parens is fully inherited from
// javascript/format.janet with zero typescript-specific additions --
// confirmed live it matches a real typescript parse unmodified.
TEST_CASE("typescript-mode's control.parens is inherited from javascript/format.janet unmodified",
          "[FormatSpacing]") {
    const Mode mode = TypeScriptMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("function f(x: number) {\n    if (x) {}\n}\n"), "control.parens")
                .size()
            == 1);
}

TEST_CASE("End to end: typescript-mode's formatCaptures drives a real space edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = TypeScriptMode();
    Buffer     buffer("test.ts");
    buffer.InsertAtPoint("function f(x: number): void {\n    if(x) {\n    }\n}\n");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "typescript", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "function f(x: number): void {\n    if (x) {\n    }\n}\n");
}

// kotlin-mode: the eleventh language. Every condition/subject here is an
// unwrapped bare expression with no spanning node at all (confirmed
// live), needing the paired mechanism even for if/while's own simple
// case, unlike every prior language where at least SOME constructs had a
// real spanning node to capture directly.
TEST_CASE("kotlin-mode's format.janet names control.parens over if/while/for/when, as a matched pair",
          "[FormatSpacing]") {
    const Mode mode = KotlinMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("fun f() {\n    if (x) {}\n}\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("fun f() {\n    while (x) {}\n}\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("fun f() {\n    for (i in xs) {}\n}\n"), "control.parens").size()
            == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("fun f() {\n    when (x) {\n    }\n}\n"), "control.parens").size()
            == 1);
}

TEST_CASE("kotlin-mode's format.janet captures a catch clause's own parens as a matched pair too",
          "[FormatSpacing]") {
    const Mode        mode   = KotlinMode();
    const std::string source = "fun f() {\n    try {\n    } catch (e: Exception) {\n    }\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 1);
}

TEST_CASE("End to end: kotlin-mode's formatCaptures drives a real space edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = KotlinMode();
    Buffer     buffer("test.kt");
    buffer.InsertAtPoint("fun f() {\n    if(x) {\n    }\n}\n");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "kotlin", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "fun f() {\n    if (x) {\n    }\n}\n");
}

// c-mode: the twelfth language. control.parens's own node type
// (`parenthesized_expression`) differs from cpp's `condition_clause`,
// confirmed live rather than assumed to carry over.
TEST_CASE("c-mode's format.janet names control.parens over if/while/switch", "[FormatSpacing]") {
    const Mode mode = CMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("void f(void) { if (x) {} }"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("void f(void) { while (x) {} }"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("void f(void) { switch (x) {} }"), "control.parens").size() == 1);
}

TEST_CASE("c-mode's format.janet captures a for-loop's own parens as a matched pair", "[FormatSpacing]") {
    const Mode        mode   = CMode();
    const std::string source = "void f(void) { for (int i = 0; i < 10; i++) {} }";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "control.parens").size() == 1);
}

TEST_CASE("End to end: c-mode's formatCaptures drives a real space edit", "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int f(int x) {\n    if(x) {\n    }\n    return 0;\n}\n");

    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "c", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int f(int x) {\n    if (x) {\n    }\n    return 0;\n}\n");
}

// lua-mode: control.parens is the same narrow "already parenthesized"
// lever python/go's own files use (verified live, same as those two --
// "if x then" produces zero matches, "if (x) then" produces exactly one).
// Unlike python (which has no brace-shaped captures at all), lua DOES
// carry brace.function/brace.control -- both synthesized from a paired
// "<name>.open"/"<name>.close" match on a multi-byte keyword token
// (Editor/FormatBracePlacement.h's own "first genuinely keyword-delimited
// language" follow-up), which is what exercises ComputeSpaceEdits'
// :within handling past a single-byte "(" for the first time: :within
// reads capture.openLength/closeLength rather than hardcoding +1/-1, so
// this needed no new C++ once that generalization existed.
TEST_CASE("lua-mode's format.janet control.parens only captures an already-parenthesized "
          "condition",
          "[FormatSpacing]") {
    const Mode mode = LuaMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("if x then\nend\n"), "control.parens").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("if (x) then\nend\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("while x do\nend\n"), "control.parens").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("while (x) do\nend\n"), "control.parens").size() == 1);
}

TEST_CASE("End to end: lua-mode's formatCaptures drives a real space edit only when parens "
          "are present",
          "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceWithin("control.parens", false);

    const Mode mode = LuaMode();

    Buffer withParens("t.lua");
    withParens.InsertAtPoint("if ( x ) then\nend\n");
    ApplyFormatTextEdits(withParens,
                         ComputeSpaceEdits(withParens.Text(), "lua", mode.formatCaptures(withParens.Text())));
    REQUIRE(withParens.Text() == "if (x) then\nend\n");

    Buffer withoutParens("t2.lua");
    withoutParens.InsertAtPoint("if x then\nend\n");
    ApplyFormatTextEdits(
        withoutParens, ComputeSpaceEdits(withoutParens.Text(), "lua", mode.formatCaptures(withoutParens.Text())));
    REQUIRE(withoutParens.Text() == "if x then\nend\n"); // nothing to touch, no crash either
}

TEST_CASE("End to end: lua-mode's :within reads a keyword delimiter's real length, never the "
          "hardcoded single byte a brace/paren capture defaults to",
          "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceWithin("lua/brace.control", true);

    const Mode mode = LuaMode();
    Buffer     buffer("t.lua");
    // No space between "y()" and "end" -- :within=true asks for exactly
    // one just inside each delimiter. A capture using
    // capture.startByte+1/endByte-1 (the pre-generalization code) would
    // corrupt this: capture.endByte-1 lands INSIDE "end" itself (between
    // 'n' and 'd'), not before it.
    buffer.InsertAtPoint("local x = 1\ndo y()end\n");
    ApplyFormatTextEdits(buffer, ComputeSpaceEdits(buffer.Text(), "lua", mode.formatCaptures(buffer.Text())));
    REQUIRE(buffer.Text() == "local x = 1\ndo y() end\n");

    SetSpaceWithin("lua/brace.control", std::nullopt);
}

// bash-mode: control.parens over test-command brackets ("[", "[[") -- a
// real, mandatory (not merely optional/redundant) construct, unlike every
// other language's own control.parens lever, added once the paired
// keyword-delimiter mechanism (Editor/FormatBracePlacement.h's own
// follow-up, proven on lua-mode) made this safe for a 2-byte "[[" too.
TEST_CASE("bash-mode's format.janet names control.parens over both single and double test "
          "brackets",
          "[FormatSpacing]") {
    const Mode mode = BashMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("if [ -f x ]; then\n    y\nfi\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("if [[ -f x ]]; then\n    y\nfi\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("for ((i=0;i<10;i++)); do\n    y\ndone\n"), "control.parens").size() ==
            1);
}

TEST_CASE("End to end: bash-mode's :within adds a space inside single AND double test "
          "brackets, or normalizes excess space down to exactly one",
          "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceWithin("bash/control.parens", true);

    const Mode mode = BashMode();

    // "[-f x]" (missing space on either side) is NOT actually valid bash
    // either -- confirmed live with a real bash RUN (not just `bash -n`):
    // "[-f: command not found". :within=true's own "insert" direction is
    // always safe regardless (it can only ever ADD separation, and a
    // real project's own source presumably already has SOME separation,
    // however much), so this remains a meaningful, reachable test even
    // though the exact zero-space starting shape below wouldn't itself
    // have come from valid bash.
    Buffer single("t.sh");
    single.InsertAtPoint("if [-f x]; then\n    y\nfi\n");
    ApplyFormatTextEdits(single, ComputeSpaceEdits(single.Text(), "bash", mode.formatCaptures(single.Text())));
    REQUIRE(single.Text() == "if [ -f x ]; then\n    y\nfi\n");

    // A capture using capture.startByte+1/endByte-1 (the pre-
    // generalization code) would land INSIDE "[[" itself (between the two
    // '[' characters), not after it.
    Buffer doubleBracket("t2.sh");
    doubleBracket.InsertAtPoint("if [[  -f x  ]]; then\n    y\nfi\n");
    ApplyFormatTextEdits(doubleBracket,
                         ComputeSpaceEdits(doubleBracket.Text(), "bash", mode.formatCaptures(doubleBracket.Text())));
    REQUIRE(doubleBracket.Text() == "if [[ -f x ]]; then\n    y\nfi\n");

    SetSpaceWithin("bash/control.parens", std::nullopt);
}

// End to end: the real hazard :within=false has for bash's test brackets
// (found live, documented in format.janet, and reported to the user) --
// now actually GUARDED, not merely documented. "["/"[[" are both ordinary
// bash WORDS needing whitespace separation from their own content --
// confirmed live with a real bash RUN, not just `bash -n`: "[ -n $x]"
// parses fine but fails at runtime ("[: missing ']'"), and "[[-f x]]"
// fails outright. A C-style for-loop's own "(("/"))" has no such
// requirement (arithmetic context, confirmed live removing its own
// interior space still runs correctly) and is UNAFFECTED by this guard.
TEST_CASE("End to end: bash-mode's :within=false is declined for single and double test "
          "brackets (removing the space would corrupt real bash), but still applies "
          "normally to a C-style for-loop's own arithmetic clause",
          "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceWithin("bash/control.parens", false);

    const Mode mode = BashMode();

    Buffer            single("t.sh");
    const std::string singleSource = "if [ -f x ]; then\n    y\nfi\n";
    single.InsertAtPoint(singleSource);
    ApplyFormatTextEdits(single, ComputeSpaceEdits(single.Text(), "bash", mode.formatCaptures(single.Text())));
    REQUIRE(single.Text() == singleSource); // NOT compacted to "[-f x]" -- that fails at runtime

    Buffer            doubleBracket("t2.sh");
    const std::string doubleSource = "if [[ -f x ]]; then\n    y\nfi\n";
    doubleBracket.InsertAtPoint(doubleSource);
    ApplyFormatTextEdits(doubleBracket,
                         ComputeSpaceEdits(doubleBracket.Text(), "bash", mode.formatCaptures(doubleBracket.Text())));
    REQUIRE(doubleBracket.Text() == doubleSource); // NOT compacted to "[[-f x]]" -- that fails to parse

    Buffer arith("t3.sh");
    arith.InsertAtPoint("for (( i=0; i<10; i++ )); do\n    y\ndone\n");
    ApplyFormatTextEdits(arith, ComputeSpaceEdits(arith.Text(), "bash", mode.formatCaptures(arith.Text())));
    REQUIRE(arith.Text() == "for ((i=0; i<10; i++)); do\n    y\ndone\n"); // safe -- arithmetic context, no hazard

    SetSpaceWithin("bash/control.parens", std::nullopt);
}

// fish-mode: brace.control's own :within=false has a DIFFERENT hazard
// shape than bash's -- not a lexer-specific reserved-word quirk, but a
// GENERAL word-byte fusion risk (Editor/FormatSpacing.h's own
// `EmitWithinIfSafe`, shared with FormatBracePlacement.h's collapse-empty
// glue via FormatEdit.h's `IsWordByte`). "begin" ends in a word byte and
// so does typical body content ("echo"), so removing that one space
// fuses them into "beginecho" -- confirmed live with a real fish RUN.
// "{" is not a word byte, so the identical :within=false direction is
// perfectly safe for the brace form.
TEST_CASE("fish-mode's :within=false is declined on begin_statement's own \"begin\" side "
          "(word-byte fusion), but applies normally to its \"{\" form",
          "[FormatSpacing]") {
    const FormatRulesGuard guard;
    SetSpaceWithin("fish/brace.control", false);

    const Mode mode = FishMode();

    Buffer            beginBuffer("t.fish");
    const std::string beginSource = "echo hi\nbegin echo x;end\n";
    beginBuffer.InsertAtPoint(beginSource);
    ApplyFormatTextEdits(beginBuffer,
                         ComputeSpaceEdits(beginBuffer.Text(), "fish", mode.formatCaptures(beginBuffer.Text())));
    REQUIRE(beginBuffer.Text() == beginSource); // NOT compacted to "beginecho" -- that fuses two words

    Buffer braceBuffer("t2.fish");
    braceBuffer.InsertAtPoint("echo hi\n{ echo x;}\n");
    ApplyFormatTextEdits(braceBuffer,
                         ComputeSpaceEdits(braceBuffer.Text(), "fish", mode.formatCaptures(braceBuffer.Text())));
    REQUIRE(braceBuffer.Text() == "echo hi\n{echo x;}\n"); // safe -- "{" is not a word byte

    SetSpaceWithin("fish/brace.control", std::nullopt);
}
