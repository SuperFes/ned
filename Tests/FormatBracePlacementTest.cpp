#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatBracePlacement.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::BracePlacement;
using ned::editor::CppMode;
using ned::editor::CSharpMode;
using ned::editor::ComputeBracePlacementEdits;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::GoMode;
using ned::editor::JavaMode;
using ned::editor::JavaScriptMode;
using ned::editor::Mode;
using ned::editor::PhpMode;
using ned::editor::PythonMode;
using ned::editor::RustMode;
using ned::editor::SetBraceCollapseEmpty;
using ned::editor::SetBraceCollapseSimple;
using ned::editor::SetBracePlacement;
using ned::text::Buffer;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetBracePlacement("brace.function", std::nullopt);
        SetBracePlacement("cpp/brace.function", std::nullopt);
        SetBraceCollapseEmpty("brace.function", std::nullopt);
        SetBraceCollapseSimple("brace.function", std::nullopt);
    }
};

// Real format.janet files now name more than one capture over the same
// source (a control statement's own brace body alongside its own condition
// parens, see cpp/javascript's format.janet's "capture-coverage-widening"
// comments) -- filter by name rather than asserting a total count.
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

TEST_CASE("cpp-mode's format.janet names brace.function over a real function body", "[FormatBracePlacement]") {
    const Mode mode = CppMode();
    REQUIRE(mode.formatCaptures); // the query is wired up at all

    const std::string source = "int f(int x) {\n    return x;\n}\n";
    // Narrowed to the one named capture this test is actually about --
    // blank-lines-kind rollout follow-up: this same source now ALSO names
    // a "def.toplevel" capture, so the full list is no longer size 1 (the
    // same "stale total-count assumption" lesson python's own "no
    // brace-shaped captures" test already taught, see FormatBlankLines).
    const auto braceFunction = CapturesNamed(mode.formatCaptures(source), "brace.function");

    REQUIRE(braceFunction.size() == 1);
    REQUIRE(braceFunction[0].startByte == source.find('{'));
    REQUIRE(braceFunction[0].endByte == source.size() - 1); // through the closing '}'
}

TEST_CASE("cpp-mode's format.janet names brace.control for if/while/for/switch/catch bodies", "[FormatBracePlacement]") {
    const Mode mode = CppMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { if (x) {\n} }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { while (x) {\n} }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { for (;;) {\n} }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { switch (x) {\n} }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { try {\n} catch (int e) {\n} }"), "brace.control").size() == 1);

    // A braceless body has no brace to place at all -- no capture at all,
    // not a degenerate zero-width one.
    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { if (x) return; }"), "brace.control").empty());
}

TEST_CASE("cpp-mode's format.janet names brace.class and brace.namespace", "[FormatBracePlacement]") {
    const Mode mode = CppMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("class C {\n};"), "brace.class").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("struct S {\n};"), "brace.class").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("namespace n {\n}"), "brace.namespace").size() == 1);
}

TEST_CASE("javascript-mode's format.janet names brace.control and brace.class too", "[FormatBracePlacement]") {
    const Mode mode = JavaScriptMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("function f() { if (x) {\n} }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function f() { switch (x) {\n} }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C {\n}"), "brace.class").size() == 1);
}

TEST_CASE("javascript-mode's format.janet also names brace.function, over a different node type", "[FormatBracePlacement]") {
    const Mode mode = JavaScriptMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "function f(x) {\n    return x;\n}\n";
    // Narrowed the same way cpp's own equivalent test above is, for the
    // same reason: this source also names "def.toplevel" now.
    const auto braceFunction = CapturesNamed(mode.formatCaptures(source), "brace.function");

    REQUIRE(braceFunction.size() == 1);
    REQUIRE(braceFunction[0].startByte == source.find('{')); // same capture name as cpp's, different grammar node
}

TEST_CASE("A per-language override actually differentiates two real languages", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::SameLine); // the shared rule
    SetBracePlacement("cpp/brace.function", BracePlacement::NextLine); // cpp's own exception

    const std::string cppSource = "int f() {\n}\n";
    const std::string jsSource  = "function f() {\n}\n";

    const std::vector<FormatTextEdit> cppEdits =
        ComputeBracePlacementEdits(cppSource, "cpp", CppMode().formatCaptures(cppSource));
    const std::vector<FormatTextEdit> jsEdits =
        ComputeBracePlacementEdits(jsSource, "javascript", JavaScriptMode().formatCaptures(jsSource));

    REQUIRE(cppEdits.size() == 1);
    REQUIRE(cppEdits[0].text == "\n"); // cpp's own override won

    REQUIRE(jsEdits.empty()); // javascript falls through to the shared rule, already SameLine
}

TEST_CASE("An unconfigured capture contributes no edit", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    const std::string      text = "int f() {\n}\n";
    const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.size() - 1}};

    REQUIRE(ComputeBracePlacementEdits(text, "cpp", captures).empty());
}

TEST_CASE("SameLine rewrites a brace on its own line onto the header's line", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::SameLine);

    const std::string text = "int f()\n{\n    return 1;\n}\n";
    const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.rfind('}') + 1}};

    const std::vector<FormatTextEdit> edits = ComputeBracePlacementEdits(text, "cpp", captures);
    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].text == " ");
    REQUIRE(text.substr(edits[0].start, edits[0].end - edits[0].start) == "\n");
}

TEST_CASE("NextLine rewrites a same-line brace onto its own line at the header's own indent", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLine);

    const std::string text = "    int f() {\n        return 1;\n    }\n";
    const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.rfind('}') + 1}};

    const std::vector<FormatTextEdit> edits = ComputeBracePlacementEdits(text, "cpp", captures);
    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].text == "\n    "); // the header's own 4-space indent, reused verbatim
}

TEST_CASE("NextLineIndented adds one indent level past the header's own indent", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLineIndented);

    const std::string text = "int f() {\n    return 1;\n}\n";
    const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.rfind('}') + 1}};

    const std::vector<FormatTextEdit> edits = ComputeBracePlacementEdits(text, "cpp", captures);
    REQUIRE(edits.size() == 2); // the opening brace's own gap, AND the closing brace's realignment
    REQUIRE(edits[0].text == "\n    "); // no header indent (column 0) + one 4-space level
}

// Regression: found live 2026-09-15 when the user asked whether anything
// else had been "declared" rather than verified. NextLineIndented is the
// one placement whose closing delimiter does NOT align with the header's
// own indent (GNU/Whitesmiths aligns it with the OPENING delimiter's own,
// deeper column instead) -- the original implementation only ever moved
// the opening delimiter, leaving a mismatched brace pair. These apply the
// edits to a REAL buffer and check the WHOLE result, specifically because
// the original bug was invisible to a test that only inspected the
// computed edit list (exactly what "NextLineIndented adds one indent
// level..." above did, and still does for the opening half).
TEST_CASE("NextLineIndented keeps the brace pair aligned with each other, top-level", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLineIndented);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("int f() {\n    return 1;\n}\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int f()\n    {\n    return 1;\n    }\n");

    // Idempotent: re-running against the result finds nothing left to do.
    REQUIRE(ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())).empty());
}

TEST_CASE("NextLineIndented keeps the brace pair aligned with each other, nested inside a class", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLineIndented);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("class C {\n    void run() {\n        return;\n    }\n};\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    // Both halves of run()'s own brace pair land at column 8 (run()'s own
    // 4-space indent + one more level) -- not column 4, which is what the
    // pre-fix bug left the closing brace at.
    REQUIRE(buffer.Text() == "class C {\n    void run()\n        {\n        return;\n        }\n};\n");
}

TEST_CASE("NextLineIndented leaves a collapsed one-line body alone", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLineIndented);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("int f() { return 1; }\n"); // closer shares its line with real content

    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    // The opening brace still moves; the closer -- not alone on its own
    // line -- is left untouched rather than guessed at (collapse-empty/
    // collapse-simple's territory, not this one).
    REQUIRE(buffer.Text() == "int f()\n    { return 1; }\n");
}

TEST_CASE("A language-scoped rule wins over the shared one", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::SameLine);
    SetBracePlacement("cpp/brace.function", BracePlacement::NextLine);

    const std::string text = "int f() {\n}\n";
    const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.rfind('}') + 1}};

    const std::vector<FormatTextEdit> edits = ComputeBracePlacementEdits(text, "cpp", captures);
    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].text == "\n"); // NextLine, not SameLine -- cpp's own override won

    // A different language falls through to the shared rule instead.
    REQUIRE(ComputeBracePlacementEdits(text, "python", captures).empty()); // already SameLine
}

TEST_CASE("Recomputing against the edited result is idempotent", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLine);

    Buffer buffer("test.cpp");
    buffer.InsertAtPoint("int f() {\n    return 1;\n}\n");

    const std::vector<FormatCapture> before = {{"brace.function", buffer.Text().find('{'), buffer.Text().rfind('}') + 1}};
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", before));
    REQUIRE(buffer.Text() == "int f()\n{\n    return 1;\n}\n");

    const std::vector<FormatCapture> after = {{"brace.function", buffer.Text().find('{'), buffer.Text().rfind('}') + 1}};
    REQUIRE(ComputeBracePlacementEdits(buffer.Text(), "cpp", after).empty());
}

TEST_CASE("ApplyFormatTextEdits applies several edits as one undo step, back to front", "[FormatBracePlacement]") {
    Buffer buffer("test.cpp");
    buffer.InsertAtPoint("aXbYc");

    std::vector<FormatTextEdit> edits;
    edits.push_back(FormatTextEdit{1, 2, "1"}); // X -> 1
    edits.push_back(FormatTextEdit{3, 4, "2"}); // Y -> 2
    ApplyFormatTextEdits(buffer, edits);

    REQUIRE(buffer.Text() == "a1b2c");
    buffer.Undo();
    REQUIRE(buffer.Text() == "aXbYc"); // one undo step undoes both edits
}

TEST_CASE("ApplyFormatTextEdits is a no-op for an empty edit list", "[FormatBracePlacement]") {
    Buffer buffer("test.cpp");
    buffer.InsertAtPoint("unchanged");
    ApplyFormatTextEdits(buffer, {});
    REQUIRE(buffer.Text() == "unchanged");
}

TEST_CASE("End to end: a real cpp Mode's formatCaptures drives a real edit", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLine);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("int f(int x) {\n    return x;\n}\n");

    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int f(int x)\n{\n    return x;\n}\n");
}

// collapse-empty: purely textual (whitespace-only content between the
// delimiters), independent of :placement -- these exercise it standalone
// (no :placement configured at all) as well as combined with each
// placement.
TEST_CASE("collapse-empty=true glues an expanded empty body onto one line", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseEmpty("brace.function", true);

    const std::string text = "int f() {\n}\n";
    const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.find('}') + 1}};
    const std::vector<FormatTextEdit> edits = ComputeBracePlacementEdits(text, "cpp", captures);

    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].text == "{}");
}

TEST_CASE("collapse-empty=false expands a glued empty body onto two lines", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseEmpty("brace.function", false);

    const std::string text = "int f() {}\n";
    const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.find('}') + 1}};
    const std::vector<FormatTextEdit> edits = ComputeBracePlacementEdits(text, "cpp", captures);

    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].text == "{\n}"); // no :placement configured -- closer defaults to the header's own indent
}

TEST_CASE("collapse-empty leaves a non-empty body alone", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseEmpty("brace.function", true);

    const std::string text = "int f() {\n    return 1;\n}\n";
    const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.rfind('}') + 1}};
    REQUIRE(ComputeBracePlacementEdits(text, "cpp", captures).empty());
}

TEST_CASE("collapse-empty is idempotent in both directions", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;

    SetBraceCollapseEmpty("brace.function", true);
    {
        const std::string text = "int f() {}\n";
        const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.find('}') + 1}};
        REQUIRE(ComputeBracePlacementEdits(text, "cpp", captures).empty()); // already collapsed
    }

    SetBraceCollapseEmpty("brace.function", false);
    {
        const std::string text = "int f() {\n}\n";
        const std::vector<FormatCapture> captures = {{"brace.function", text.find('{'), text.find('}') + 1}};
        REQUIRE(ComputeBracePlacementEdits(text, "cpp", captures).empty()); // already expanded
    }
}

TEST_CASE("collapse-empty=true composes with :placement on a real buffer", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLine);
    SetBraceCollapseEmpty("brace.function", true);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("int f() {\n}\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    // The opening brace still moves to its own line (NextLine); the now-
    // empty body glues onto that same line rather than staying expanded.
    REQUIRE(buffer.Text() == "int f()\n{}\n");

    REQUIRE(ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())).empty());
}

TEST_CASE("collapse-empty=false composes with NextLineIndented's own closer alignment", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLineIndented);
    SetBraceCollapseEmpty("brace.function", false);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("int f() {}\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    // Both halves of the pair land at the SAME (header + one level) column
    // -- the shared ClosingIndentFor helper is what keeps this and the
    // ordinary multi-line NextLineIndented case from disagreeing.
    REQUIRE(buffer.Text() == "int f()\n    {\n    }\n");
}

// collapse-simple: capture.isSimple is a structural fact set by Mode.cpp's
// "<name>.simple" marker correlation over the real grammar -- these first
// confirm the correlation itself is right before testing what
// ComputeBracePlacementEdits does with it.
TEST_CASE("cpp-mode's format.janet marks a single-statement body isSimple, not a multi-statement one", "[FormatBracePlacement]") {
    const Mode mode = CppMode();

    const auto simpleCaptures = CapturesNamed(mode.formatCaptures("void f() { return; }"), "brace.function");
    REQUIRE(simpleCaptures.size() == 1);
    REQUIRE(simpleCaptures[0].isSimple);

    const auto multiCaptures = CapturesNamed(mode.formatCaptures("void f() { g(); return; }"), "brace.function");
    REQUIRE(multiCaptures.size() == 1);
    REQUIRE_FALSE(multiCaptures[0].isSimple);

    // Mutually exclusive with isEmpty by construction -- the marker query
    // requires "exactly one" child, an empty body has zero.
    const auto emptyCaptures = CapturesNamed(mode.formatCaptures("void f() { }"), "brace.function");
    REQUIRE(emptyCaptures.size() == 1);
    REQUIRE_FALSE(emptyCaptures[0].isSimple);
}

TEST_CASE("isSimple is correct even when the one statement is itself a nested block", "[FormatBracePlacement]") {
    const Mode mode = CppMode();
    // The function's OWN body has exactly one statement (an if); what that
    // if itself contains is irrelevant -- the marker is field-anchored to
    // the function's own body, not recursive.
    const auto captures = CapturesNamed(mode.formatCaptures("void f() { if (x) { g(); h(); } }"), "brace.function");
    REQUIRE(captures.size() == 1);
    REQUIRE(captures[0].isSimple);
}

TEST_CASE("collapse-simple=true joins an expanded single-statement body onto one line", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseSimple("brace.function", true);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("void f() {\n    return;\n}\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "void f() { return; }\n");
    REQUIRE(ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())).empty());
}

TEST_CASE("collapse-simple=false expands a one-line single-statement body", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseSimple("brace.function", false);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("void f() { return; }\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "void f() {\n    return;\n}\n");
    REQUIRE(ComputeBracePlacementEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())).empty());
}

TEST_CASE("collapse-simple leaves a multi-statement body alone", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseSimple("brace.function", true);

    const Mode mode = CppMode();
    const std::string text = "void f() {\n    g();\n    return;\n}\n";
    REQUIRE(ComputeBracePlacementEdits(text, "cpp", mode.formatCaptures(text)).empty());
}

TEST_CASE("collapse-simple=true declines to join a statement that already spans multiple lines", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseSimple("brace.function", true);

    const Mode mode = CppMode();
    // One statement, but it already spans two physical lines -- joining it
    // would risk mangling a meaningfully-broken call/comment, so this is
    // declined rather than force-joined.
    const std::string text = "void f() {\n    g(a,\n      b);\n}\n";
    REQUIRE(ComputeBracePlacementEdits(text, "cpp", mode.formatCaptures(text)).empty());
}

TEST_CASE("collapse-simple composes with :placement and with :collapse-empty on real buffers", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLine);
    SetBraceCollapseSimple("brace.function", true);
    SetBraceCollapseEmpty("brace.function", true);

    const Mode mode = CppMode();

    Buffer simpleBuffer("test.cpp");
    simpleBuffer.InsertAtPoint("int f() {\n    return 1;\n}\n");
    ApplyFormatTextEdits(simpleBuffer,
                         ComputeBracePlacementEdits(simpleBuffer.Text(), "cpp", mode.formatCaptures(simpleBuffer.Text())));
    REQUIRE(simpleBuffer.Text() == "int f()\n{ return 1; }\n");

    Buffer emptyBuffer("test.cpp");
    emptyBuffer.InsertAtPoint("int f() {\n}\n");
    ApplyFormatTextEdits(emptyBuffer,
                         ComputeBracePlacementEdits(emptyBuffer.Text(), "cpp", mode.formatCaptures(emptyBuffer.Text())));
    REQUIRE(emptyBuffer.Text() == "int f()\n{}\n");
}

TEST_CASE("javascript-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode mode = JavaScriptMode();
    const auto captures = CapturesNamed(mode.formatCaptures("function f() { return; }"), "brace.function");
    REQUIRE(captures.size() == 1);
    REQUIRE(captures[0].isSimple);
}

// java-mode: the third language over the full template, verified live
// against tree-sitter-java (including a multi-init/update for-loop, which
// javascript/cpp don't have) before this landed.
TEST_CASE("java-mode's format.janet names the full capture set over a real method", "[FormatBracePlacement]") {
    const Mode mode = JavaMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "class C {\n"
                               "  void m() {\n"
                               "    if (x) {\n"
                               "      return;\n"
                               "    }\n"
                               "  }\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    REQUIRE(CapturesNamed(captures, "brace.function").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.control").size() == 1); // the if's own body
    REQUIRE(CapturesNamed(captures, "brace.class").size() == 1);
}

TEST_CASE("java-mode's format.janet handles a for-loop with several init/update expressions", "[FormatBracePlacement]") {
    const Mode mode = JavaMode();
    const std::string source = "class C { void m() { for (int i = 0, j = 1; i < 10; ++i, --j) {} } }";
    // brace.control fires for the for-loop's own body, same as any other
    // control-flow construct -- the interesting part is proven in
    // FormatSpacingTest.cpp (the paired parens capture finding the OUTER
    // "(" ")" despite the multiple comma-separated init/update expressions).
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "brace.control").size() == 1);
}

TEST_CASE("java-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode mode = JavaMode();
    const std::string simple = "class C { void m() { return; } }";
    REQUIRE(CapturesNamed(mode.formatCaptures(simple), "brace.function")[0].isSimple);

    const std::string multi = "class C { void m() { g(); return; } }";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(multi), "brace.function")[0].isSimple);
}

TEST_CASE("End to end: java-mode's formatCaptures drives real edits across all three features", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);
    SetBraceCollapseSimple("brace.control", true);

    const Mode mode = JavaMode();
    Buffer     buffer("test.java");
    buffer.InsertAtPoint("class C {\n"
                         "  void m() {\n"
                         "    if (x)\n"
                         "    {\n"
                         "      return;\n"
                         "    }\n"
                         "  }\n"
                         "}\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "java", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "class C {\n"
                             "  void m() {\n"
                             "    if (x) { return; }\n"
                             "  }\n"
                             "}\n");

    SetBracePlacement("brace.control", std::nullopt);
    SetBraceCollapseSimple("brace.control", std::nullopt);
}

// python-mode: verified live against tree-sitter-python's own
// node-types.json that every compound statement's body is a bare "block"
// field with no wrapping delimiter tokens at all -- indentation alone
// marks the extent. python/format.janet therefore names no brace.*/
// collapse-* captures whatsoever, so this whole pass is a structural no-op
// for Python regardless of what placement/collapse rules a project
// configures -- there is simply nothing here for it to act on. (Python's
// format.janet does name def.toplevel/def.method -- the blank-lines-kind
// pilot captures, Editor/FormatBlankLines.h's own territory, not this
// pass' -- so the list as a whole is no longer expected to be empty.)
TEST_CASE("python-mode's format.janet names no brace-shaped captures at all", "[FormatBracePlacement]") {
    const Mode        mode   = PythonMode();
    const std::string source = "def f(x):\n"
                                "    if x:\n"
                                "        pass\n"
                                "class C:\n"
                                "    def m(self):\n"
                                "        pass\n";
    for (const char* name : {"brace.function", "brace.control", "brace.class"}) {
        REQUIRE(CapturesNamed(mode.formatCaptures(source), name).empty());
    }
}

TEST_CASE("End to end: brace-placement rules are a no-op on python-mode even when configured",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLineIndented);
    SetBraceCollapseSimple("brace.control", true);

    const Mode  mode   = PythonMode();
    const std::string source = "def f(x):\n    if x:\n        pass\n";
    Buffer      buffer("test.py");
    buffer.InsertAtPoint(source);

    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "python", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == source);

    SetBracePlacement("brace.function", std::nullopt);
    SetBraceCollapseSimple("brace.control", std::nullopt);
}

// go-mode: the fourth brace-carrying language. Its own real edge, verified
// live with a real `go build` before this shipped: Go performs automatic
// semicolon insertion after a `)` at end-of-line, so `func f()` followed
// by `{` on the NEXT line is a genuine compile error, not merely a style
// deviation -- confirmed with `./main.go:N: syntax error: unexpected
// semicolon or newline before {`. FormatBracePlacement.cpp's
// PlacementUnsafeForLanguage neutralizes a NextLine/NextLineIndented
// :placement rule for "go" specifically (JavaScript's own ASI does not
// fire after `)`, verified the same way, so it is unaffected).
TEST_CASE("go-mode's format.janet names the full capture set over a real file", "[FormatBracePlacement]") {
    const Mode mode = GoMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "package main\n"
                               "type I interface {\n"
                               "\tM()\n"
                               "}\n"
                               "type T struct {\n"
                               "\tX int\n"
                               "}\n"
                               "func f() {\n"
                               "\tif x {\n"
                               "\t\treturn\n"
                               "\t}\n"
                               "\tswitch x {\n"
                               "\tcase 1:\n"
                               "\t}\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    REQUIRE(CapturesNamed(captures, "brace.function").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.control").size() == 2); // the if's body and the switch's own braces
    REQUIRE(CapturesNamed(captures, "brace.class").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.interface").size() == 1);
}

TEST_CASE("go-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode        mode   = GoMode();
    const std::string simple = "package main\nfunc f() {\n\treturn\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(simple), "brace.function")[0].isSimple);

    const std::string multi = "package main\nfunc f() {\n\tg()\n\treturn\n}\n";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(multi), "brace.function")[0].isSimple);
}

TEST_CASE("go-mode's format.janet does not name a for-loop's own clause at all", "[FormatBracePlacement]") {
    // Unlike cpp/javascript/java, a for-loop's three-part clause has NO
    // wrapping parens in Go's own grammar -- writing them is a syntax
    // error, not merely non-idiomatic (verified live), so there is no
    // paired-parens capture for it here. brace.control still fires for
    // the loop's own BODY.
    const Mode        mode   = GoMode();
    const std::string source = "package main\nfunc f() {\n\tfor i := 0; i < 10; i++ {\n\t}\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "brace.control").size() == 1);
}

TEST_CASE("End to end: a NextLine :placement is a safe no-op on go-mode (ASI-unsafe)", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::NextLine);

    const Mode        mode   = GoMode();
    const std::string source = "package main\nfunc f() {\n\treturn\n}\n";
    Buffer            buffer("test.go");
    buffer.InsertAtPoint(source);

    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "go", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == source); // NOT rewritten to Allman -- that would fail to compile

    SetBracePlacement("brace.function", std::nullopt);
}

// Deliberately NOT testing a "brace already on its own line" starting
// shape here the way the same test does for every other language: that
// shape is exactly the ASI-broken one the guard above exists for, and
// tree-sitter-go's own parser performs the same automatic semicolon
// insertion the real compiler does (verified live: `mode.formatCaptures`
// returns ZERO captures for that shape -- the query's own
// `body: (block)` field simply never resolves, since the parser doesn't
// attach the orphaned `{...}` as the function's body at all). So Go
// SOURCE SHAPED like a NextLine rewrite is invisible to this pass twice
// over -- the guard, and separately the query never matching it in the
// first place. What SameLine still legitimately fixes for Go is
// horizontal spacing around an already-correctly-placed brace.
TEST_CASE("End to end: SameLine :placement still applies normally on go-mode", "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::SameLine);

    const Mode mode = GoMode();
    Buffer     buffer("test.go");
    buffer.InsertAtPoint("package main\nfunc f()   {\n\treturn\n}\n");

    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "go", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "package main\nfunc f() {\n\treturn\n}\n");

    SetBracePlacement("brace.function", std::nullopt);
}

TEST_CASE("End to end: a language-scoped go/ override is unaffected by the ASI guard on a safe placement",
          "[FormatBracePlacement]") {
    // The guard only neutralizes an UNSAFE placement -- it must never
    // interfere with a normal, safe one, scoped or not.
    const FormatRulesGuard guard;
    SetBracePlacement("go/brace.function", BracePlacement::SameLine);

    const Mode mode = GoMode();
    Buffer     buffer("test.go");
    buffer.InsertAtPoint("package main\nfunc f()   {\n\treturn\n}\n");

    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "go", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "package main\nfunc f() {\n\treturn\n}\n");

    SetBracePlacement("go/brace.function", std::nullopt);
}

// php-mode: the sixth language, and the first with a genuine THREE-way
// body ambiguity per construct (brace / colon-alternate / bare unbraced
// statement) rather than the two-way ones every prior language had.
// Requiring `(compound_statement)` as the field's own TYPE is what
// discriminates live, verified for every shape before this shipped.
TEST_CASE("php-mode's format.janet only captures the brace-bodied shape of if/while/for",
          "[FormatBracePlacement]") {
    const Mode mode = PhpMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("<?php\nif ($x) {\n    return;\n}\n"), "brace.control").size() == 1);
    REQUIRE(
        CapturesNamed(mode.formatCaptures("<?php\nif ($x):\n    return;\nendif;\n"), "brace.control").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("<?php\nif ($x) return;\n"), "brace.control").empty());
}

// elseif/else follow-up: else_if_clause/else_clause carry the EXACT same
// three-way body shape if_statement itself has -- so a FULL colon-syntax
// chain ("if (x): ... elseif (y): ... else: ... endif;") must produce
// ZERO brace.control matches across every clause, not just the leading
// "if", while the equivalent all-brace chain produces one PER clause.
TEST_CASE("php-mode's format.janet captures elseif/else bodies too, declining the full colon-alternate chain",
          "[FormatBracePlacement]") {
    const Mode mode = PhpMode();

    const std::string braceChain = "<?php\nif ($x) {\n    a();\n} elseif ($y) {\n    b();\n} else {\n    c();\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(braceChain), "brace.control").size() == 3);

    const std::string colonChain =
        "<?php\nif ($x):\n    a();\nelseif ($y):\n    b();\nelse:\n    c();\nendif;\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(colonChain), "brace.control").empty());
}

TEST_CASE("php-mode's format.janet marks an elseif/else body isSimple too", "[FormatBracePlacement]") {
    const Mode mode = PhpMode();

    const std::string simple = "<?php\nif ($x) {\n} elseif ($y) {\n    return 1;\n} else {\n    return 2;\n}\n";
    const auto        control = CapturesNamed(mode.formatCaptures(simple), "brace.control");
    REQUIRE(control.size() == 3);
    REQUIRE_FALSE(control[0].isSimple); // if's own body is empty, not "simple" (isEmpty/isSimple are exclusive)
    REQUIRE(control[1].isSimple);       // elseif's body
    REQUIRE(control[2].isSimple);       // else's body
}

// switch_block is ONE node type in PHP's grammar representing BOTH its
// brace form and its own colon-alternate form ("switch (x): ... endswitch;")
// -- there is no second node type to discriminate by the way if/while's
// own colon_block vs. compound_statement split allows. Confirmed live
// this is a real hazard: capturing the bare node would sometimes hand
// ComputeBracePlacementEdits a capture whose first byte is ':' instead of
// '{'. The paired "{"/"}" token capture sidesteps it structurally --
// verified live the colon form produces zero matches, not a false one.
TEST_CASE("php-mode's format.janet only captures switch's brace form, never its colon-alternate form",
          "[FormatBracePlacement]") {
    const Mode mode = PhpMode();

    const auto braceForm =
        CapturesNamed(mode.formatCaptures("<?php\nswitch ($x) {\ncase 1:\n    break;\n}\n"), "brace.control");
    REQUIRE(braceForm.size() == 1);

    const auto colonForm = CapturesNamed(
        mode.formatCaptures("<?php\nswitch ($x):\ncase 1:\n    break;\nendswitch;\n"), "brace.control");
    REQUIRE(colonForm.empty());
}

TEST_CASE("php-mode's format.janet names the full capture set over a real file", "[FormatBracePlacement]") {
    const Mode mode = PhpMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "<?php\n"
                               "interface I {\n"
                               "    public function m();\n"
                               "}\n"
                               "trait T {\n"
                               "    public function shared() {\n"
                               "        return 1;\n"
                               "    }\n"
                               "}\n"
                               "class C {\n"
                               "    public function f($x) {\n"
                               "        if ($x) {\n"
                               "            return;\n"
                               "        }\n"
                               "        try {\n"
                               "        } catch (Exception $e) {\n"
                               "            log($e);\n"
                               "        }\n"
                               "    }\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    REQUIRE(CapturesNamed(captures, "brace.function").size() == 2); // shared() and f()
    REQUIRE(CapturesNamed(captures, "brace.control").size() == 2);  // the if's body and the catch's body
    REQUIRE(CapturesNamed(captures, "brace.class").size() == 2);    // class C and trait T
    REQUIRE(CapturesNamed(captures, "brace.interface").size() == 1);
}

TEST_CASE("php-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode        mode   = PhpMode();
    const std::string simple = "<?php\nfunction f() {\n    return 1;\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(simple), "brace.function")[0].isSimple);

    const std::string multi = "<?php\nfunction f() {\n    g();\n    return 1;\n}\n";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(multi), "brace.function")[0].isSimple);
}

TEST_CASE("End to end: php-mode's formatCaptures drives real edits across all three features",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);
    SetBraceCollapseSimple("brace.control", true);

    const Mode mode = PhpMode();
    Buffer     buffer("test.php");
    buffer.InsertAtPoint("<?php\n"
                         "function f($x) {\n"
                         "    if ($x)\n"
                         "    {\n"
                         "        return;\n"
                         "    }\n"
                         "}\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "php", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "<?php\n"
                             "function f($x) {\n"
                             "    if ($x) { return; }\n"
                             "}\n");
}

// rust-mode: the seventh language. No new structural hazard -- Rust's
// grammar carries neither Go's ASI nor PHP's per-construct body ambiguity
// -- but two real judgment calls: struct/enum/impl all fold into
// brace.class (a JetBrains-style "type body" grouping), and a for-loop's
// own iterable is deliberately NOT eligible for control.parens even
// though the grammar allows wrapping it (see format.janet's own comment).
TEST_CASE("rust-mode's format.janet names the full capture set over a real file", "[FormatBracePlacement]") {
    const Mode mode = RustMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "trait T {\n"
                               "    fn shared(&self) {\n"
                               "        return;\n"
                               "    }\n"
                               "}\n"
                               "struct S {\n"
                               "    x: i32,\n"
                               "}\n"
                               "impl S {\n"
                               "    fn f(&self, x: i32) {\n"
                               "        if x > 0 {\n"
                               "            return;\n"
                               "        }\n"
                               "        match x {\n"
                               "            1 => {}\n"
                               "            _ => {}\n"
                               "        }\n"
                               "    }\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    REQUIRE(CapturesNamed(captures, "brace.function").size() == 2); // shared() and f()
    // Only 2, not 4: match's OWN braces are captured, but a match_arm's own
    // "{}" value is an ordinary block-typed expression, not one of this
    // file's captured parent shapes -- verified live, a deliberate scope
    // cut (an arm body is the "value:" side of a match_arm, matched by
    // nothing here) rather than a hidden gap.
    REQUIRE(CapturesNamed(captures, "brace.control").size() == 2); // if's body, match's own outer braces
    REQUIRE(CapturesNamed(captures, "brace.class").size() == 2);   // struct S and impl S
    REQUIRE(CapturesNamed(captures, "brace.interface").size() == 1);
}

TEST_CASE("rust-mode's format.janet names brace.namespace over a mod's own body", "[FormatBracePlacement]") {
    const Mode mode = RustMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("mod m {\n    struct X;\n}\n"), "brace.namespace").size() == 1);
    // A bodyless "mod foo;" file-per-module declaration has no `body` field at all.
    REQUIRE(CapturesNamed(mode.formatCaptures("mod m;\n"), "brace.namespace").empty());
}

TEST_CASE("rust-mode's format.janet does not capture a unit or tuple struct, only a real braced body",
          "[FormatBracePlacement]") {
    const Mode mode = RustMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("struct Unit;\n"), "brace.class").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("struct Tup(i32, i32);\n"), "brace.class").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("struct Empty {}\n"), "brace.class").size() == 1);
}

TEST_CASE("rust-mode's format.janet does not name a distinct capture for an else/else-if branch",
          "[FormatBracePlacement]") {
    // Matches cpp/javascript/java's own scope cut, not PHP's colon hazard --
    // an else-if's own body is captured because it's itself a nested
    // if_expression's "consequence" (recursion, not a dedicated pattern),
    // but the trailing bare "else { ... }" is not captured at all.
    const Mode        mode   = RustMode();
    const std::string source = "fn f() {\n"
                               "    if x {\n"
                               "        a();\n"
                               "    } else if y {\n"
                               "        b();\n"
                               "    } else {\n"
                               "        c();\n"
                               "    }\n"
                               "}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "brace.control").size() == 2); // if's body, else-if's body
}

TEST_CASE("rust-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode        mode   = RustMode();
    const std::string simple = "fn f() {\n    return;\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(simple), "brace.function")[0].isSimple);

    const std::string multi = "fn f() {\n    g();\n    return;\n}\n";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(multi), "brace.function")[0].isSimple);
}

TEST_CASE("End to end: rust-mode's formatCaptures drives real edits across all three features",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);
    SetBraceCollapseSimple("brace.control", true);

    const Mode mode = RustMode();
    Buffer     buffer("test.rs");
    buffer.InsertAtPoint("fn f(x: i32) {\n"
                         "    if x > 0\n"
                         "    {\n"
                         "        return;\n"
                         "    }\n"
                         "}\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "rust", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "fn f(x: i32) {\n"
                             "    if x > 0 { return; }\n"
                             "}\n");
}

// csharp-mode: the eighth language. No new structural hazard, but a real
// wrinkle: unlike cpp/java/javascript (whose condition field IS a node
// spanning the whole "(...)"), c#'s own if/while/switch condition/value is
// a bare expression field with the parens as unwrapped anonymous tokens --
// confirmed against grammar.json, not assumed -- so those need the SAME
// paired mechanism a for-loop's own clause does everywhere else, even
// though c#'s parens are mandatory, not an optional python/go-style lever.
TEST_CASE("csharp-mode's format.janet names the full capture set over a real file", "[FormatBracePlacement]") {
    const Mode mode = CSharpMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "namespace N {\n"
                               "    interface I {\n"
                               "        void M();\n"
                               "    }\n"
                               "    class C {\n"
                               "        void F() {\n"
                               "            if (x) {\n"
                               "                return;\n"
                               "            }\n"
                               "        }\n"
                               "    }\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    REQUIRE(CapturesNamed(captures, "brace.function").size() == 1); // F()
    REQUIRE(CapturesNamed(captures, "brace.control").size() == 1);  // the if's body
    REQUIRE(CapturesNamed(captures, "brace.class").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.interface").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.namespace").size() == 1);
}

TEST_CASE("csharp-mode's format.janet does not capture a bodyless positional record",
          "[FormatBracePlacement]") {
    const Mode mode = CSharpMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("record R(int X, int Y);\n"), "brace.class").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("record class R { void M() {} }\n"), "brace.class").size() == 1);
}

TEST_CASE("csharp-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode        mode   = CSharpMode();
    const std::string simple = "class C {\n    void M() {\n        return;\n    }\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(simple), "brace.function")[0].isSimple);

    const std::string multi = "class C {\n    void M() {\n        G();\n        return;\n    }\n}\n";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(multi), "brace.function")[0].isSimple);
}

TEST_CASE("End to end: csharp-mode's formatCaptures drives real edits across all three features",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);
    SetBraceCollapseSimple("brace.control", true);

    const Mode mode = CSharpMode();
    Buffer     buffer("test.cs");
    buffer.InsertAtPoint("class C {\n"
                         "    void M(int x) {\n"
                         "        if (x > 0)\n"
                         "        {\n"
                         "            return;\n"
                         "        }\n"
                         "    }\n"
                         "}\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "csharp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "class C {\n"
                             "    void M(int x) {\n"
                             "        if (x > 0) { return; }\n"
                             "    }\n"
                             "}\n");
}
