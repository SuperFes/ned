#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatBracePlacement.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::BashMode;
using ned::editor::BracePlacement;
using ned::editor::CMode;
using ned::editor::ComputeBracePlacementEdits;
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
using ned::editor::SetBraceCollapseEmpty;
using ned::editor::SetBraceCollapseSimple;
using ned::editor::SetBracePlacement;
using ned::editor::TsxMode;
using ned::editor::TypeScriptMode;
using ned::text::Buffer;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetBracePlacement("brace.function", std::nullopt);
        SetBracePlacement("cpp/brace.function", std::nullopt);
        SetBraceCollapseEmpty("brace.function", std::nullopt);
        SetBraceCollapseSimple("brace.function", std::nullopt);
        // test-isolation follow-up: several pre-existing tests below set a
        // bare "brace.control" rule (placement/collapse-empty/collapse-
        // simple) relying on THIS guard for cleanup, but it only ever
        // covered brace.function -- found live via an intermittent,
        // order-dependent failure once a bash-mode test needed brace.
        // control's placement to be a genuine no-op and got a leaked
        // collapse-simple=true from an earlier php-mode test instead. Also
        // covers bash/lua's own multi-byte-delimiter placement, since
        // several new tests below set "brace.control" scoped to those
        // languages too.
        SetBracePlacement("brace.control", std::nullopt);
        SetBraceCollapseEmpty("brace.control", std::nullopt);
        SetBraceCollapseSimple("brace.control", std::nullopt);
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
    // coverage-audit follow-up: this used to assert 1 (catch's own body
    // only) -- try_statement's OWN body ("try { }" itself) now also
    // fires, a real gap this rollout's own audit found, not a regression.
    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { try {\n} catch (int e) {\n} }"), "brace.control").size() == 2);

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

// typescript-mode: the ninth language, but embedded as a DELTA over
// javascript/format.janet rather than a from-scratch file -- every
// shared construct (function/class/if/control.parens/def.method's own
// method case) uses byte-identical node types to plain JavaScript,
// confirmed live, so typescript/language.janet's own :queries entry
// concatenates javascript/format.janet directly instead of duplicating
// it. typescript/format.janet itself names only what JS's grammar has no
// equivalent for: interface/enum/abstract-class/type-alias.
TEST_CASE("typescript-mode inherits javascript's own brace.function/control/class captures unmodified",
          "[FormatBracePlacement]") {
    const Mode mode = TypeScriptMode();

    const std::string source = "function f(x: number): number {\n"
                               "    if (x > 0) {\n"
                               "        return x;\n"
                               "    }\n"
                               "    return 0;\n"
                               "}\n"
                               "class C {\n"
                               "    m(): void {}\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);
    // coverage-audit follow-up: this used to assert 1 -- method_
    // definition's own body ("m(): void {}") now also gets brace.
    // function, a real gap this rollout's own audit found (previously
    // ZERO placement capture existed for any class method at all), not a
    // regression in this inheritance mechanism.
    REQUIRE(CapturesNamed(captures, "brace.function").size() == 2);
    REQUIRE(CapturesNamed(captures, "brace.control").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.class").size() == 1);
}

TEST_CASE("typescript-mode's own format.janet names brace.interface, and brace.class over an "
          "abstract_class_declaration (a distinct node type from class_declaration)",
          "[FormatBracePlacement]") {
    const Mode mode = TypeScriptMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("interface I {\n    m(): void;\n}\n"), "brace.interface").size()
            == 1);
    // A bare class_declaration pattern (javascript's own) does NOT match
    // this distinct node type -- confirmed live it needed its own
    // pattern in typescript/format.janet, not inherited for free.
    REQUIRE(CapturesNamed(mode.formatCaptures("abstract class A {\n    abstract m(): void;\n}\n"), "brace.class")
                .size()
            == 1);
}

TEST_CASE("End to end: typescript-mode's formatCaptures drives a real edit combining inherited and "
          "typescript-only captures",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);
    SetBraceCollapseSimple("brace.control", true);

    const Mode mode = TypeScriptMode();
    Buffer     buffer("test.ts");
    buffer.InsertAtPoint("function f(x: number): number {\n"
                         "    if (x > 0)\n"
                         "    {\n"
                         "        return x;\n"
                         "    }\n"
                         "    return 0;\n"
                         "}\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "typescript", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "function f(x: number): number {\n"
                             "    if (x > 0) { return x; }\n"
                             "    return 0;\n"
                             "}\n");
}

TEST_CASE("tsx-mode inherits the same typescript+javascript capture set (via :queries-from + its own "
          "duplicated :format entry)",
          "[FormatBracePlacement]") {
    const Mode        mode   = TsxMode();
    const std::string source = "interface P {\n"
                               "    x: number;\n"
                               "}\n"
                               "function C(): JSX.Element {\n"
                               "    if (x) {\n"
                               "        return <div>hi</div>;\n"
                               "    }\n"
                               "    return <div>bye</div>;\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);
    REQUIRE(CapturesNamed(captures, "brace.interface").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.function").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.control").size() == 1);
}

// kotlin-mode: the eleventh language, and a real outlier -- tree-sitter-
// kotlin declares ZERO fields anywhere in the grammar (confirmed live),
// so every capture here is a bare node-type match. A genuinely new
// hazard for this rollout: function_body/control_structure_body are each
// the SAME node type for a real "{ ... }" block AND Kotlin's own
// brace-less single-expression form ("fun f() = x + 1",
// "if (x) 1 else 2") -- fixed with this codebase's ":match?" text
// predicate (checking the captured span starts with "{"), the first
// format capture in this whole rollout needing one rather than pure
// structure.
TEST_CASE("kotlin-mode's format.janet does not capture an expression-bodied function at all",
          "[FormatBracePlacement]") {
    const Mode mode = KotlinMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("fun f(x: Int) = x + 1\n"), "brace.function").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("fun f() {}\n"), "brace.function").size() == 1);
}

TEST_CASE("kotlin-mode's format.janet does not capture a brace-less if/else branch, but captures a "
          "braced one -- and captures BOTH branches uniformly (no field to distinguish them)",
          "[FormatBracePlacement]") {
    const Mode mode = KotlinMode();
    REQUIRE(
        CapturesNamed(mode.formatCaptures("fun f() {\n    if (x) 1 else 2\n}\n"), "brace.control").empty());

    const std::string braced = "fun f() {\n    if (x) {\n        a()\n    } else {\n        b()\n    }\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(braced), "brace.control").size() == 2);
}

TEST_CASE("kotlin-mode's format.janet names brace.class over class/interface/object bodies alike -- "
          "there is no separate interface_declaration node type in this grammar at all",
          "[FormatBracePlacement]") {
    const Mode mode = KotlinMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("class C {\n}\n"), "brace.class").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("interface I {\n    fun m(): Unit\n}\n"), "brace.class").size()
            == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("object O {\n}\n"), "brace.class").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("interface I {\n}\n"), "brace.interface").empty());
}

TEST_CASE("kotlin-mode's format.janet captures a catch clause's own braces via the paired mechanism, "
          "over a construct with no distinct body-wrapper node at all",
          "[FormatBracePlacement]") {
    const Mode        mode   = KotlinMode();
    const std::string source = "fun f() {\n    try {\n    } catch (e: Exception) {\n        log(e)\n    }\n}\n";
    const auto         caps   = CapturesNamed(mode.formatCaptures(source), "brace.control");
    REQUIRE(caps.size() == 1); // try's own body is deliberately not captured, matching every prior language
    REQUIRE(source.substr(caps[0].startByte, 1) == "{");
}

TEST_CASE("kotlin-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode        mode   = KotlinMode();
    const std::string simple = "fun f() {\n    a()\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(simple), "brace.function")[0].isSimple);

    const std::string multi = "fun f() {\n    a()\n    b()\n}\n";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(multi), "brace.function")[0].isSimple);
}

TEST_CASE("End to end: kotlin-mode's formatCaptures drives real edits across all three features",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);
    SetBraceCollapseSimple("brace.control", true);

    const Mode mode = KotlinMode();
    Buffer     buffer("test.kt");
    buffer.InsertAtPoint("fun f(x: Int) {\n"
                         "    if (x > 0)\n"
                         "    {\n"
                         "        println(x)\n"
                         "    }\n"
                         "}\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "kotlin", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "fun f(x: Int) {\n"
                             "    if (x > 0) { println(x) }\n"
                             "}\n");
}

// c-mode: the twelfth language. tree-sitter-c is a SEPARATE grammar from
// tree-sitter-cpp, not shared -- verified live rather than assumed, and a
// real difference found doing so: C's own if/while/switch condition field
// is typed `parenthesized_expression`, not cpp's own `condition_clause`.
TEST_CASE("c-mode's format.janet names the full capture set over a real file", "[FormatBracePlacement]") {
    const Mode mode = CMode();
    REQUIRE(mode.formatCaptures);

    const std::string source = "int f(int x) {\n"
                               "    if (x > 0) {\n"
                               "        return x;\n"
                               "    }\n"
                               "    switch (x) {\n"
                               "    case 1:\n"
                               "        break;\n"
                               "    }\n"
                               "    return 0;\n"
                               "}\n"
                               "struct S {\n"
                               "    int x;\n"
                               "};\n"
                               "union U {\n"
                               "    int x;\n"
                               "};\n";
    const auto captures = mode.formatCaptures(source);

    REQUIRE(CapturesNamed(captures, "brace.function").size() == 1);
    REQUIRE(CapturesNamed(captures, "brace.control").size() == 2); // the if's body and the switch's own braces
    REQUIRE(CapturesNamed(captures, "brace.class").size() == 2);   // struct S and union U
}

TEST_CASE("c-mode's format.janet does not capture a bodyless function declaration at all",
          "[FormatBracePlacement]") {
    const Mode mode = CMode();
    REQUIRE(mode.formatCaptures("int f(int x);\n").empty());
}

TEST_CASE("c-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode        mode   = CMode();
    const std::string simple = "int f(void) {\n    return 1;\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(simple), "brace.function")[0].isSimple);

    const std::string multi = "int f(void) {\n    g();\n    return 1;\n}\n";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(multi), "brace.function")[0].isSimple);
}

TEST_CASE("End to end: c-mode's formatCaptures drives real edits across all three features",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);
    SetBraceCollapseSimple("brace.control", true);

    const Mode mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int f(int x) {\n"
                         "    if (x > 0)\n"
                         "    {\n"
                         "        return x;\n"
                         "    }\n"
                         "    return 0;\n"
                         "}\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "c", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int f(int x) {\n"
                             "    if (x > 0) { return x; }\n"
                             "    return 0;\n"
                             "}\n");
}

// bash-mode: originally shipped as a genuinely PARTIAL case (only
// function_definition was brace-delimited, no brace.control/control.parens
// at all) since if/while/for/case use keyword delimiters (then/fi,
// do/done, in/esac) rather than single-character braces. Revisited once
// lua-mode's own openLength/closeLength generalization (see below) proved
// out the mechanism for multi-byte delimiters -- bash now carries the
// full set too, via the exact same paired mechanism.
TEST_CASE("bash-mode's format.janet names brace.function over all three function syntaxes",
          "[FormatBracePlacement]") {
    const Mode mode = BashMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("f() {\n    echo hi\n}\n"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function g {\n    echo hi\n}\n"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function h() {\n    echo hi\n}\n"), "brace.function").size() == 1);
}

TEST_CASE("bash-mode's format.janet still captures a nested function's own braces, even though "
          "only the outer one is def.toplevel",
          "[FormatBracePlacement]") {
    const Mode        mode   = BashMode();
    const std::string source = "f() {\n    g() {\n        echo inner\n    }\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "brace.function").size() == 2);
}

TEST_CASE("bash-mode's format.janet marks a single-statement body isSimple too", "[FormatBracePlacement]") {
    const Mode        mode   = BashMode();
    const std::string simple = "f() {\n    echo hi\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(simple), "brace.function")[0].isSimple);

    const std::string multi = "f() {\n    echo hi\n    echo bye\n}\n";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(multi), "brace.function")[0].isSimple);
}

TEST_CASE("End to end: bash-mode's formatCaptures drives a real brace-placement edit",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::SameLine);

    const Mode mode = BashMode();
    Buffer     buffer("test.sh");
    buffer.InsertAtPoint("f()\n{\n    echo hi\n}\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "bash", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "f() {\n    echo hi\n}\n");
}

// bash-mode revisit: brace.control/control.parens, added once the
// openLength/closeLength generalization (below, lua-mode's own follow-up)
// proved out the paired keyword-delimiter mechanism.
TEST_CASE("bash-mode's format.janet names brace.control over while/for/if/case, spanning an "
          "elif/else chain to the one real closing token",
          "[FormatBracePlacement]") {
    const Mode mode = BashMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("while true; do\n    echo hi\ndone\n"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("until false; do\n    echo hi\ndone\n"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("for i in 1 2 3; do\n    echo $i\ndone\n"), "brace.control").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("select o in a b; do\n    echo $o\ndone\n"), "brace.control").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("if true; then\n    echo hi\nfi\n"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("case $x in\n    a) echo 1 ;;\nesac\n"), "brace.control").size() == 1);

    const std::string chained  = "if true; then\n    a\nelif false; then\n    b\nelse\n    c\nfi\n";
    const auto        captures = CapturesNamed(mode.formatCaptures(chained), "brace.control");
    REQUIRE(captures.size() == 1);
    REQUIRE(captures[0].startByte == chained.find("then"));
    REQUIRE(captures[0].endByte == chained.rfind("fi") + 2);
}

TEST_CASE("bash-mode's format.janet reports real multi-byte delimiter lengths for do/done, "
          "then/fi, and in/esac",
          "[FormatBracePlacement]") {
    const Mode mode = BashMode();

    const auto doneCap = CapturesNamed(mode.formatCaptures("while true; do\n    x\ndone\n"), "brace.control");
    REQUIRE(doneCap[0].openLength == 2);  // "do"
    REQUIRE(doneCap[0].closeLength == 4); // "done"

    const auto fiCap = CapturesNamed(mode.formatCaptures("if true; then\n    x\nfi\n"), "brace.control");
    REQUIRE(fiCap[0].openLength == 4);  // "then"
    REQUIRE(fiCap[0].closeLength == 2); // "fi"

    const auto esacCap = CapturesNamed(mode.formatCaptures("case $x in\n    a) x ;;\nesac\n"), "brace.control");
    REQUIRE(esacCap[0].openLength == 2);  // "in"
    REQUIRE(esacCap[0].closeLength == 4); // "esac"
}

TEST_CASE("bash-mode's format.janet has a .simple marker for do/done bodies (its own node span "
          "starts at \"do\" and ends at \"done\") but not for then/fi or in/esac (neither "
          "starts its own span at the paired keyword)",
          "[FormatBracePlacement]") {
    const Mode mode = BashMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("while true; do\n    echo hi\ndone\n"), "brace.control")[0].isSimple);
    REQUIRE_FALSE(
        CapturesNamed(mode.formatCaptures("while true; do\n    echo hi\n    echo bye\ndone\n"), "brace.control")[0]
            .isSimple);
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures("if true; then\n    echo hi\nfi\n"), "brace.control")[0].isSimple);
    REQUIRE_FALSE(
        CapturesNamed(mode.formatCaptures("case $x in\n    a) echo 1 ;;\nesac\n"), "brace.control")[0].isSimple);
}

TEST_CASE("bash-mode's format.janet names brace.control for a C-style for-loop's own brace "
          "body, a subshell, and a standalone group command, all with .simple support",
          "[FormatBracePlacement]") {
    const Mode mode = BashMode();

    const auto cStyleBrace =
        CapturesNamed(mode.formatCaptures("for ((i=0;i<10;i++)) { echo $i; }\n"), "brace.control");
    REQUIRE(cStyleBrace.size() == 1);
    REQUIRE(cStyleBrace[0].isSimple);

    // the do/done alternative is a genuinely different construct, captured
    // by the shared do_group pattern, not this one.
    REQUIRE(CapturesNamed(mode.formatCaptures("for ((i=0;i<10;i++)); do echo $i; done\n"), "brace.control").size() ==
            1);

    const auto subshellCap = CapturesNamed(mode.formatCaptures("( echo hi )\n"), "brace.control");
    REQUIRE(subshellCap.size() == 1);
    REQUIRE(subshellCap[0].isSimple);

    const auto groupCap = CapturesNamed(mode.formatCaptures("{ echo hi; }\n"), "brace.control");
    REQUIRE(groupCap.size() == 1);
    REQUIRE(groupCap[0].isSimple);
}

TEST_CASE("bash-mode's format.janet excludes a function's own compound_statement body and a "
          "C-style for-loop's own from the generic standalone-group-command capture, so "
          "neither is double-captured under two different names",
          "[FormatBracePlacement]") {
    const Mode mode = BashMode();

    const std::string source = "f() {\n    echo hi\n}\n";
    const auto        fn     = CapturesNamed(mode.formatCaptures(source), "brace.function");
    REQUIRE(fn.size() == 1);
    for (const auto& bc : CapturesNamed(mode.formatCaptures(source), "brace.control")) {
        REQUIRE_FALSE((bc.startByte == fn[0].startByte && bc.endByte == fn[0].endByte));
    }

    // a group command genuinely NESTED inside a function body (not AS its
    // body) must still be captured -- #not-has-parent? checks the
    // IMMEDIATE parent only, so this is unaffected by the exclusion above.
    const std::string nested = "f() {\n    echo hi\n    { echo nested; }\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(nested), "brace.control").size() == 1);
}

TEST_CASE("bash-mode's format.janet names control.parens over test-command brackets and a "
          "C-style for-loop's own arithmetic clause",
          "[FormatBracePlacement]") {
    const Mode mode = BashMode();

    const auto singleBracket = CapturesNamed(mode.formatCaptures("if [ -f x ]; then\n    y\nfi\n"), "control.parens");
    REQUIRE(singleBracket.size() == 1);
    REQUIRE(singleBracket[0].openLength == 1);  // "["
    REQUIRE(singleBracket[0].closeLength == 1); // "]"

    const auto doubleBracket =
        CapturesNamed(mode.formatCaptures("if [[ -f x ]]; then\n    y\nfi\n"), "control.parens");
    REQUIRE(doubleBracket.size() == 1);
    REQUIRE(doubleBracket[0].openLength == 2);  // "[["
    REQUIRE(doubleBracket[0].closeLength == 2); // "]]"

    const auto arithClause =
        CapturesNamed(mode.formatCaptures("for ((i=0;i<10;i++)); do\n    x\ndone\n"), "control.parens");
    REQUIRE(arithClause.size() == 1);
    REQUIRE(arithClause[0].openLength == 2);  // "(("
    REQUIRE(arithClause[0].closeLength == 2); // "))"
}

TEST_CASE("End to end: bash-mode's collapse-empty on do/done inserts a real separating space, "
          "the same word-byte hazard lua-mode's own do/end capture has",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseEmpty("bash/brace.control", true);

    const Mode mode = BashMode();
    Buffer     buffer("t.sh");
    buffer.InsertAtPoint("x=1\nwhile true; do\ndone\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "bash", mode.formatCaptures(buffer.Text())));

    // "do done" is still NOT valid bash on its own (a real do/done body
    // must be non-empty, confirmed live with `bash -n`) -- this test
    // exercises the mechanism's own correctness (no token fusion into
    // "dodone"), not a claim that the result is runnable. See this
    // capture's own format.janet comment for why that's not a hazard.
    REQUIRE(buffer.Text() == "x=1\nwhile true; do done\n");

    SetBraceCollapseEmpty("bash/brace.control", std::nullopt);
}

TEST_CASE("End to end: bash-mode's collapse-empty on case/esac is real, reachable, and "
          "produces valid bash (unlike do/done, an empty case IS legal)",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseEmpty("bash/brace.control", true);

    const Mode mode = BashMode();
    Buffer     buffer("t.sh");
    buffer.InsertAtPoint("x=1\ncase $x in\nesac\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "bash", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "x=1\ncase $x in esac\n");

    SetBraceCollapseEmpty("bash/brace.control", std::nullopt);
}

TEST_CASE("End to end: bash-mode's NextLineIndented repositions the real multi-byte closer "
          "token, not one hardcoded byte before the capture ends",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("bash/brace.control", BracePlacement::NextLineIndented);

    const Mode mode = BashMode();
    Buffer     buffer("t.sh");
    buffer.InsertAtPoint("x=1\nwhile true; do\necho hi\ndone\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "bash", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "x=1\nwhile true;\n  do\necho hi\n  done\n");

    SetBracePlacement("bash/brace.control", std::nullopt);
}

// PlacementUnsafeForLanguage's second real hazard (after Go's own ASI
// one) -- the INVERSE shape: SameLine specifically is what's dangerous
// here, every other placement is safe, and it's scoped to brace.control
// only (bash's real-brace brace.function is unaffected). `do`/`then` are
// bash reserved words needing a real statement TERMINATOR (";" or a
// newline) before them, never merely whitespace -- confirmed live with a
// real `bash -n`: "while true do"/"if true then" are hard syntax errors.
TEST_CASE("End to end: a SameLine :placement is a safe no-op on bash-mode's brace.control "
          "(do/then need a real terminator, not just whitespace)",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);

    const Mode mode = BashMode();

    Buffer            doBuffer("test.sh");
    const std::string doSource = "while true\ndo\n    echo hi\ndone\n";
    doBuffer.InsertAtPoint(doSource);
    ApplyFormatTextEdits(doBuffer,
                         ComputeBracePlacementEdits(doBuffer.Text(), "bash", mode.formatCaptures(doBuffer.Text())));
    REQUIRE(doBuffer.Text() == doSource); // NOT glued onto "while true do" -- that fails to parse

    Buffer            thenBuffer("test2.sh");
    const std::string thenSource = "if true\nthen\n    echo hi\nfi\n";
    thenBuffer.InsertAtPoint(thenSource);
    ApplyFormatTextEdits(
        thenBuffer, ComputeBracePlacementEdits(thenBuffer.Text(), "bash", mode.formatCaptures(thenBuffer.Text())));
    REQUIRE(thenBuffer.Text() == thenSource);

    SetBracePlacement("brace.control", std::nullopt);
}

TEST_CASE("End to end: the bash brace.control SameLine guard is scoped to brace.control -- "
          "brace.function (a real brace, no terminator needed) is unaffected",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::SameLine);

    const Mode mode = BashMode();
    Buffer     buffer("test.sh");
    buffer.InsertAtPoint("f()\n{\n    echo hi\n}\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "bash", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "f() {\n    echo hi\n}\n");

    SetBracePlacement("brace.function", std::nullopt);
}

TEST_CASE("End to end: a language-scoped bash/brace.control override is still caught by the "
          "SameLine guard, not just the bare name",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("bash/brace.control", BracePlacement::SameLine);

    const Mode        mode = BashMode();
    Buffer            buffer("test.sh");
    const std::string source = "while true\ndo\n    echo hi\ndone\n";
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "bash", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == source);

    SetBracePlacement("bash/brace.control", std::nullopt);
}

// lua-mode: the fourteenth language, and the first whose delimiters are
// KEYWORD tokens rather than single characters -- FormatCapture::
// openLength/closeLength (Mode.h) and every place FormatBracePlacement.cpp
// touched a delimiter byte were generalized past a hardcoded single byte
// before this file was written, verified live against ned_tests's own
// suite (4653 cases, unchanged) before any of these tests were added.
TEST_CASE("lua-mode's format.janet reports the real multi-byte delimiter lengths", "[FormatBracePlacement]") {
    const Mode mode = LuaMode();

    const auto fn = CapturesNamed(mode.formatCaptures("function f()\n    return 1\nend\n"), "brace.function");
    REQUIRE(fn.size() == 1);
    REQUIRE(fn[0].openLength == 1);  // ")"
    REQUIRE(fn[0].closeLength == 3); // "end"

    const auto doEnd = CapturesNamed(mode.formatCaptures("while x do\n    y()\nend\n"), "brace.control");
    REQUIRE(doEnd.size() == 1);
    REQUIRE(doEnd[0].openLength == 2);  // "do"
    REQUIRE(doEnd[0].closeLength == 3); // "end"

    const auto thenEnd = CapturesNamed(mode.formatCaptures("if x then\n    y()\nend\n"), "brace.control");
    REQUIRE(thenEnd.size() == 1);
    REQUIRE(thenEnd[0].openLength == 4);  // "then"
    REQUIRE(thenEnd[0].closeLength == 3); // "end"
}

TEST_CASE("lua-mode's format.janet names brace.function over every function-declaration shape",
          "[FormatBracePlacement]") {
    const Mode mode = LuaMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("function f()\n    return 1\nend\n"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("local function f()\n    return 1\nend\n"), "brace.function").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function M.f()\n    return 1\nend\n"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function M:f()\n    return 1\nend\n"), "brace.function").size() == 1);
    // an empty parameter list is still exactly one capture -- the closing
    // ")" is always present even with zero parameters.
    REQUIRE(CapturesNamed(mode.formatCaptures("function f()\nend\n"), "brace.function").size() == 1);
    // a bare function EXPRESSION (never a statement-level definition) is
    // deliberately left uncaptured for brace.function, matching every
    // other language's own "declarations, not expressions" scope -- Lua's
    // function_definition has the identical parameters/end shape and
    // WOULD match if captured (an earlier draft of format.janet did
    // capture it, caught by this very assertion before it shipped), so
    // this is a real, checked scope cut, not an oversight.
    REQUIRE(CapturesNamed(mode.formatCaptures("local f = function()\n    return 1\nend\n"), "brace.function")
                .empty());
}

TEST_CASE("lua-mode's format.janet names brace.control over while/for/do/if, one capture "
          "per construct even through an elseif/else chain",
          "[FormatBracePlacement]") {
    const Mode mode = LuaMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("while x do\n    y()\nend\n"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("for i = 1, 10 do\n    y()\nend\n"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("for k, v in pairs(t) do\n    y()\nend\n"), "brace.control").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("do\n    local z = 1\nend\n"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("if x then\n    y()\nend\n"), "brace.control").size() == 1);

    // elseif/else: still exactly ONE brace.control, spanning "then" all
    // the way to the chain's own final "end" -- verified live this is
    // structurally sound (the outer if_statement owns the only literal
    // "end" token regardless of how many elseif branches sit inside it),
    // not merely that it doesn't crash.
    const std::string chained         = "if x then\n    a()\nelseif y then\n    b()\nelse\n    c()\nend\n";
    const auto        chainedCaptures = CapturesNamed(mode.formatCaptures(chained), "brace.control");
    REQUIRE(chainedCaptures.size() == 1);
    REQUIRE(chainedCaptures[0].startByte == chained.find("then"));
    REQUIRE(chainedCaptures[0].endByte == chained.rfind("end") + 3);
}

TEST_CASE("lua-mode's format.janet control.parens only fires when the condition is actually "
          "parenthesized",
          "[FormatBracePlacement]") {
    const Mode mode = LuaMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("if x then\nend\n"), "control.parens").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("if (x) then\nend\n"), "control.parens").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("while x do\nend\n"), "control.parens").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("while (x) do\nend\n"), "control.parens").size() == 1);
}

TEST_CASE("lua-mode's format.janet has no brace.class/brace.interface/def.method/.simple "
          "at all -- real language absences",
          "[FormatBracePlacement]") {
    const Mode mode = LuaMode();

    // a plausible-looking "class" written as a table plus dot-methods --
    // Lua has no dedicated class syntax the grammar could ever name.
    const std::string source = "local M = {}\n"
                               "function M.new()\n    return setmetatable({}, M)\nend\n"
                               "function M:method()\n    return 1\nend\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "brace.class").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "brace.interface").empty());
    // no def.method: a "method" here is a def.toplevel like any other
    // function, never nested inside a distinct class-body container.
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "def.method").empty());
    // no .simple marker anywhere -- every capture in this file is
    // synthesized from a paired open/close match with no single node to
    // anchor a second ".simple" pattern against.
    for (const auto& capture : mode.formatCaptures(source)) {
        REQUIRE_FALSE(capture.isSimple);
    }
}

TEST_CASE("lua-mode's format.janet def.toplevel covers every declaration shape but not an "
          "anonymous function expression",
          "[FormatBracePlacement]") {
    const Mode mode = LuaMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("function f()\nend\n"), "def.toplevel").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("local function f()\nend\n"), "def.toplevel").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function M.f()\nend\n"), "def.toplevel").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("local f = function()\nend\n"), "def.toplevel").empty());
}

TEST_CASE("lua-mode's format.janet .first marker fires for the chunk's own first "
          "declaration, wired at exactly the level def.toplevel itself uses",
          "[FormatBracePlacement]") {
    const Mode mode = LuaMode();

    // A single top-level function with nothing before it in the chunk.
    const auto solo = CapturesNamed(mode.formatCaptures("function f()\nend\n"), "def.toplevel");
    REQUIRE(solo.size() == 1);
    REQUIRE(solo[0].isFirst);

    // A real preceding statement disqualifies isFirst -- the same lesson
    // every prior language's own leading-construct case already taught
    // (Go's "package main", cpp's leading #include, Bash's shebang, ...).
    const std::string withPreamble = "local x = 1\nfunction f()\nend\nfunction g()\nend\n";
    const auto        defs         = CapturesNamed(mode.formatCaptures(withPreamble), "def.toplevel");
    REQUIRE(defs.size() == 2);
    REQUIRE_FALSE(defs[0].isFirst);
    REQUIRE_FALSE(defs[1].isFirst);

    // A function nested inside an arbitrary block (do/if/while/a function
    // body) is NOT itself def.toplevel-scoped for ".first" purposes --
    // wired only at chunk level, the same level def.toplevel's own
    // primary capture uses, matching every other language's own .first
    // convention (python/go/js all wire it at the identical module/
    // class-body level their def.toplevel/def.method itself uses, never
    // at a generic nested block) -- verified live an earlier draft got
    // this wrong.
    const std::string nested = "do\n    function f()\n    end\nend\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(nested), "def.toplevel").empty());
}

TEST_CASE("End to end: lua-mode's formatCaptures drives a real brace-placement edit on the "
          "keyword pair",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("lua/brace.control", BracePlacement::NextLineIndented);

    const Mode mode = LuaMode();
    Buffer     buffer("t.lua");
    buffer.InsertAtPoint("while x do\nprint(1)\nend\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "lua", mode.formatCaptures(buffer.Text())));

    // "do" moves to its own indented line; "end" (a genuinely 3-byte
    // token, not the single byte every prior language's own equivalent
    // test exercised) is repositioned to the SAME indent -- the real
    // regression test for the whole multi-byte-delimiter generalization.
    // Body content is untouched (brace placement never reindents a body).
    REQUIRE(buffer.Text() == "while x\n  do\nprint(1)\n  end\n");

    SetBracePlacement("lua/brace.control", std::nullopt);
}

TEST_CASE("End to end: lua-mode's collapse-empty glues a keyword pair with a real separating "
          "space, never fusing the two tokens into one word",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseEmpty("lua/brace.control", true);

    const Mode mode = LuaMode();
    Buffer     buffer("t.lua");
    // a real preceding statement -- ComputeBracePlacementEdits declines a
    // capture starting at byte 0 outright ("nothing could precede a
    // capture at offset 0"), the same guard every other language's own
    // end-to-end test already has to route around.
    buffer.InsertAtPoint("local x = 1\ndo\nend\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "lua", mode.formatCaptures(buffer.Text())));

    // "do"+"end" glued with NO separator would read back as the single
    // identifier "doend" -- a real correctness hazard unique to keyword
    // delimiters that a brace/paren language's own "{}"/"()" glue never
    // faces. IsWordByte's boundary check in FormatBracePlacement.cpp
    // inserts the space that keeps this two tokens, not one.
    REQUIRE(buffer.Text() == "local x = 1\ndo end\n");

    SetBraceCollapseEmpty("lua/brace.control", std::nullopt);
}

TEST_CASE("End to end: lua-mode's collapse-empty on brace.function glues \")\" directly onto "
          "\"end\" with no inserted space",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseEmpty("lua/brace.function", true);

    const Mode mode = LuaMode();
    Buffer     buffer("t.lua");
    buffer.InsertAtPoint("function f()\nend\n");
    ApplyFormatTextEdits(buffer,
                         ComputeBracePlacementEdits(buffer.Text(), "lua", mode.formatCaptures(buffer.Text())));

    // ")" is not a word byte, so no space is inserted -- matching every
    // brace language's own "{}" glue convention (no cosmetic space,
    // only what correctness requires).
    REQUIRE(buffer.Text() == "function f()end\n");

    SetBraceCollapseEmpty("lua/brace.function", std::nullopt);
}

// coverage-audit follow-up: repeat_statement ("repeat ... until cond") --
// a real, distinct delimiter pair the original Lua rollout never touched
// (every pass focused on if/while/for/do's own "do"/"end" shape).
TEST_CASE("lua-mode's format.janet names brace.control over repeat/until, reporting the real "
          "multi-byte delimiter lengths",
          "[FormatBracePlacement]") {
    const Mode mode = LuaMode();

    const auto caps = CapturesNamed(mode.formatCaptures("repeat\n    f()\nuntil x > 3\n"), "brace.control");
    REQUIRE(caps.size() == 1);
    REQUIRE(caps[0].openLength == 6);  // "repeat"
    REQUIRE(caps[0].closeLength == 5); // "until"
    // repeat_statement's own node span does NOT end at "until" (it
    // continues through the trailing condition), unlike do_group's own
    // symmetric span -- so no .simple marker exists here, matching every
    // other paired capture in this file.
    REQUIRE_FALSE(caps[0].isSimple);
}

TEST_CASE("End to end: lua-mode's formatCaptures drives a real brace-placement edit on "
          "repeat/until (SameLine squeezes the gap before \"repeat\" itself, the same "
          "\"opens with its own very first token, no header\" shape this file's own bare "
          "do_statement already has -- verified live this joined form is valid, running Lua)",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("lua/brace.control", BracePlacement::SameLine);

    const Mode mode = LuaMode();
    Buffer     buffer("t.lua");
    buffer.InsertAtPoint("x = 1\nrepeat\n    f()\nuntil x > 3\n");
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "lua", mode.formatCaptures(buffer.Text())));
    REQUIRE(buffer.Text() == "x = 1 repeat\n    f()\nuntil x > 3\n");

    // test-isolation follow-up: FormatRulesGuard only resets the BARE
    // "brace.control" key -- a language-scoped override like this one
    // must be reset explicitly, the same lesson a leaked bash-mode
    // collapse-simple rule already taught this file once (see
    // project memory's own bash-revisit follow-up).
    SetBracePlacement("lua/brace.control", std::nullopt);
}

// fish-mode: the fifteenth language, and an even more minimal partial
// case than bash's own original shape -- if/while/for/switch have NO
// capturable brace.control at all (their own header ends directly in the
// SAME terminator that separates any two ordinary statements, with no
// separate movable opening keyword the way bash's "do"/"then" are).
// function_definition is NOT in that group (coverage-audit follow-up: an
// earlier writeup wrongly lumped it in) -- it has a real, literal
// "function"/"end" pair, captured below alongside begin_statement's own.
TEST_CASE("fish-mode's format.janet has no brace.control at all for if/while/for/switch -- a "
          "real absence, not a scope cut -- but function_definition DOES get brace.function "
          "(coverage-audit follow-up: this rollout's own earlier writeup wrongly lumped it in "
          "with the other four, which share a genuinely different reason for their absence)",
          "[FormatBracePlacement]") {
    const Mode mode = FishMode();

    const std::string controlOnly = "if true\n"
                                    "    echo yes\n"
                                    "else\n"
                                    "    echo no\n"
                                    "end\n"
                                    "while true\n"
                                    "    echo loop\n"
                                    "end\n"
                                    "for i in 1 2 3\n"
                                    "    echo $i\n"
                                    "end\n"
                                    "switch $x\n"
                                    "    case 1\n"
                                    "        echo one\n"
                                    "end\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(controlOnly), "brace.function").empty());
    // brace.control is real, but ONLY for begin_statement -- none of the
    // constructs above contribute to it.
    REQUIRE(CapturesNamed(mode.formatCaptures(controlOnly), "brace.control").empty());

    const auto fn = CapturesNamed(mode.formatCaptures("function f\n    echo hi\nend\n"), "brace.function");
    REQUIRE(fn.size() == 1);
    REQUIRE(fn[0].openLength == 8);  // "function"
    REQUIRE(fn[0].closeLength == 3); // "end"
    REQUIRE(fn[0].isSimple);
    REQUIRE_FALSE(
        CapturesNamed(mode.formatCaptures("function f\n    echo hi\n    echo bye\nend\n"), "brace.function")[0]
            .isSimple);
    // A "-d" description option between the name and the body is a real,
    // if uncommon, shape this file's own .simple anchor deliberately
    // declines rather than risks a false positive on -- conservatively
    // "not simple", never a wrong "simple".
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures("function f -d 'desc'\n    echo hi\nend\n"), "brace.function")[0]
                      .isSimple);
}

TEST_CASE("fish-mode's format.janet names brace.control over both begin_statement forms, "
          "reporting the real multi-byte delimiter lengths",
          "[FormatBracePlacement]") {
    const Mode mode = FishMode();

    const auto beginEnd = CapturesNamed(mode.formatCaptures("begin\n    echo hi\nend\n"), "brace.control");
    REQUIRE(beginEnd.size() == 1);
    REQUIRE(beginEnd[0].openLength == 5);  // "begin"
    REQUIRE(beginEnd[0].closeLength == 3); // "end"

    const auto braced = CapturesNamed(mode.formatCaptures("{\n    echo hi\n}\n"), "brace.control");
    REQUIRE(braced.size() == 1);
    REQUIRE(braced[0].openLength == 1);  // "{"
    REQUIRE(braced[0].closeLength == 1); // "}"
}

TEST_CASE("fish-mode's format.janet has a .simple marker for BOTH begin_statement forms "
          "(unlike bash's own then/fi and in/esac, begin_statement's own node span starts "
          "and ends exactly at its paired tokens for either form)",
          "[FormatBracePlacement]") {
    const Mode mode = FishMode();

    REQUIRE(CapturesNamed(mode.formatCaptures("begin\n    echo hi\nend\n"), "brace.control")[0].isSimple);
    REQUIRE_FALSE(
        CapturesNamed(mode.formatCaptures("begin\n    echo hi\n    echo bye\nend\n"), "brace.control")[0].isSimple);
    REQUIRE(CapturesNamed(mode.formatCaptures("{\n    echo hi\n}\n"), "brace.control")[0].isSimple);
    REQUIRE_FALSE(
        CapturesNamed(mode.formatCaptures("{\n    echo hi\n    echo bye\n}\n"), "brace.control")[0].isSimple);
}

TEST_CASE("fish-mode's format.janet names def.toplevel over every function, not a nested "
          "one, with correct .first markers",
          "[FormatBracePlacement]") {
    const Mode mode = FishMode();

    const std::string source   = "function f\n"
                                 "    function inner\n"
                                 "        echo inner\n"
                                 "    end\n"
                                 "end\n"
                                 "function g\n"
                                 "    echo hi\n"
                                 "end\n";
    const auto        toplevel = CapturesNamed(mode.formatCaptures(source), "def.toplevel");
    REQUIRE(toplevel.size() == 2); // f() and g() -- the nested inner() is not its own def.toplevel
    REQUIRE(toplevel[0].isFirst);
    REQUIRE_FALSE(toplevel[1].isFirst);

    REQUIRE(CapturesNamed(mode.formatCaptures(source), "def.method").empty());
}

// End to end: the SAME "SameLine glues onto a preceding statement's own
// terminator" hazard bash's own do/then turned out to have -- confirmed
// live with a real fish RUN, not assumed to transfer just because the
// shape looks similar.
TEST_CASE("End to end: a SameLine :placement is a safe no-op on fish-mode's brace.control "
          "(begin_statement is a bare statement needing a real terminator before it)",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);

    const Mode mode = FishMode();

    Buffer            beginBuffer("test.fish");
    const std::string beginSource = "echo hi\nbegin\n    echo x\nend\n";
    beginBuffer.InsertAtPoint(beginSource);
    ApplyFormatTextEdits(
        beginBuffer, ComputeBracePlacementEdits(beginBuffer.Text(), "fish", mode.formatCaptures(beginBuffer.Text())));
    REQUIRE(beginBuffer.Text() == beginSource); // NOT glued onto "echo hi begin" -- "begin" reads as an argument then

    Buffer            braceBuffer("test2.fish");
    const std::string braceSource = "echo hi\n{\n    echo x\n}\n";
    braceBuffer.InsertAtPoint(braceSource);
    ApplyFormatTextEdits(
        braceBuffer, ComputeBracePlacementEdits(braceBuffer.Text(), "fish", mode.formatCaptures(braceBuffer.Text())));
    REQUIRE(braceBuffer.Text() == braceSource);

    SetBracePlacement("brace.control", std::nullopt);
}

// coverage-audit follow-up: the IDENTICAL hazard for brace.function --
// function_definition is a bare, standalone statement too, so a SameLine
// gap after a preceding statement is equally dangerous ("'end' outside
// of a block", confirmed live). PlacementUnsafeForLanguage now guards
// both capture names for fish, unlike bash (whose own brace.function is
// unaffected -- real braces, no terminator needed).
TEST_CASE("End to end: a SameLine :placement is a safe no-op on fish-mode's brace.function too",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBracePlacement("brace.function", BracePlacement::SameLine);

    const Mode        mode = FishMode();
    Buffer            buffer("test.fish");
    const std::string source = "echo hi\nfunction f\n    echo x\nend\n";
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, ComputeBracePlacementEdits(buffer.Text(), "fish", mode.formatCaptures(buffer.Text())));
    REQUIRE(buffer.Text() == source); // NOT glued onto "echo hi function" -- "function" reads as an argument then
}

// coverage-audit follow-up: a real corruption hazard found live via this
// exact apply-and-check-whole-result discipline, not by inspection --
// collapse-simple's own interior computation assumes the open TOKEN sits
// directly beside the real body, true everywhere else in this codebase
// but false for fish's own function_definition (name:/option: fields sit
// between "function" and the body). Both directions are declined for
// "fish"/"brace.function" specifically (CollapseSimpleUnsafeForLanguage).
TEST_CASE("End to end: fish-mode's collapse-simple is declined (both directions) for "
          "brace.function -- its own interior computation would otherwise split "
          "\"function\" from the function's own name",
          "[FormatBracePlacement]") {
    const Mode mode = FishMode();

    {
        const FormatRulesGuard guard;
        SetBraceCollapseSimple("fish/brace.function", false); // force-expand direction
        Buffer            buffer("t.fish");
        const std::string source = "echo pre\nfunction greet; echo hello; end\n";
        buffer.InsertAtPoint(source);
        ApplyFormatTextEdits(buffer,
                             ComputeBracePlacementEdits(buffer.Text(), "fish", mode.formatCaptures(buffer.Text())));
        // NOT "function\n    greet; echo hello;\nend" -- fish -n confirmed
        // that shape is a hard syntax error ("function" names nothing).
        REQUIRE(buffer.Text() == source);
        SetBraceCollapseSimple("fish/brace.function", std::nullopt);
    }
    {
        const FormatRulesGuard guard;
        SetBraceCollapseSimple("fish/brace.function", true); // join direction
        Buffer            buffer("t2.fish");
        const std::string source = "echo pre\nfunction greet\n    echo hello\nend\n";
        buffer.InsertAtPoint(source);
        ApplyFormatTextEdits(buffer,
                             ComputeBracePlacementEdits(buffer.Text(), "fish", mode.formatCaptures(buffer.Text())));
        // NOT "function greet echo hello end" -- would fold the name into
        // the reconstructed "body" text.
        REQUIRE(buffer.Text() == source);
        SetBraceCollapseSimple("fish/brace.function", std::nullopt);
    }
}

TEST_CASE("End to end: fish-mode's collapse-empty glues begin/end with a real separating "
          "space (word-byte fusion), but the brace form glues with none",
          "[FormatBracePlacement]") {
    const FormatRulesGuard guard;
    SetBraceCollapseEmpty("fish/brace.control", true);

    const Mode mode = FishMode();

    Buffer beginBuffer("t.fish");
    beginBuffer.InsertAtPoint("echo hi\nbegin\nend\n");
    ApplyFormatTextEdits(
        beginBuffer, ComputeBracePlacementEdits(beginBuffer.Text(), "fish", mode.formatCaptures(beginBuffer.Text())));
    REQUIRE(beginBuffer.Text() == "echo hi\nbegin end\n");

    Buffer braceBuffer("t2.fish");
    braceBuffer.InsertAtPoint("echo hi\n{\n}\n");
    ApplyFormatTextEdits(
        braceBuffer, ComputeBracePlacementEdits(braceBuffer.Text(), "fish", mode.formatCaptures(braceBuffer.Text())));
    REQUIRE(braceBuffer.Text() == "echo hi\n{}\n");

    SetBraceCollapseEmpty("fish/brace.control", std::nullopt);
}

// coverage-audit follow-up (see project memory): a dedicated audit pass
// found constructs this rollout's own per-language passes skipped
// because they weren't the day's focus, not because the grammar lacks
// them -- plus a deliberate policy reversal (anonymous function/lambda/
// closure bodies now get brace.function everywhere, matching declared
// functions' own placement, superseding the earlier "declarations, not
// expressions" scope cut).

TEST_CASE("cpp-mode's format.janet now covers do-while/try's-own-body/union/lambda/extern-C",
          "[FormatBracePlacement]") {
    const Mode mode = CppMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { do { g(); } while (x); }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("void f() { try { g(); } catch (int e) {} }"), "brace.control")
                .size() == 2); // the try block's own body AND catch's
    REQUIRE(CapturesNamed(mode.formatCaptures("union U { int a; float b; };"), "brace.class").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("enum E { A, B };"), "def.toplevel").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("auto f = [](int x) { return x; };"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("extern \"C\" { void f(); }"), "brace.namespace").size() == 1);
    // extern "C" f(); (single-declaration form, no braces) must NOT match.
    REQUIRE(CapturesNamed(mode.formatCaptures("extern \"C\" void f();"), "brace.namespace").empty());
}

TEST_CASE("c-mode's format.janet now covers do-while", "[FormatBracePlacement]") {
    REQUIRE(CapturesNamed(CMode().formatCaptures("void f() { do { g(); } while (x); }"), "brace.control").size() ==
            1);
}

TEST_CASE("java-mode's format.janet now covers do-while/try's-own-body/finally/static-and-"
          "instance-initializers/lambda/anonymous-class/enum-constant-body/switch-arrow-arm",
          "[FormatBracePlacement]") {
    const Mode mode = JavaMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void f() { do { g(); } while (x); } }"), "brace.control")
                .size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void f() { try { g(); } finally { h(); } } }"),
                          "brace.control")
                .size() == 2); // the try block's own body AND finally's
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { static { init(); } }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { { init(); } }"), "brace.control").size() == 1);
    REQUIRE(
        CapturesNamed(mode.formatCaptures("class C { Runnable r = () -> { g(); }; }"), "brace.function").size() ==
        1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { Object o = new Object() { void f() {} }; }"), "brace.class")
                .size() == 2); // C itself AND the anonymous class body
    REQUIRE(CapturesNamed(mode.formatCaptures("enum E { A { void f() {} } }"), "brace.class").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void f(int x) { switch (x) { case 1 -> { g(); } } } }"),
                          "brace.control")
                .size() == 2); // switch's own body AND the arrow-arm's
}

TEST_CASE("csharp-mode's format.janet now covers do-while/try's-own-body/finally/constructor/"
          "destructor/property-accessor/local-function/using-lock-fixed/unsafe-checked/lambda/"
          "anonymous-method/switch-expression",
          "[FormatBracePlacement]") {
    const Mode mode = CSharpMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void F() { do { G(); } while (x); } }"), "brace.control")
                .size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void F() { try { G(); } finally { H(); } } }"),
                          "brace.control")
                .size() == 2);
    // Previously ONLY method_declaration got brace.function -- the
    // single highest-impact C# gap this audit found.
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { C() { G(); } }"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { ~C() { G(); } }"), "brace.function").size() == 1);
    // Both accessors have real (if trivial) blocks here -- both fire.
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { int X { get { return 1; } set { } } }"), "brace.function")
                .size() == 2);
    // A true auto-property accessor ("set;", no braces at all) has no
    // block body at all -- must not match.
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { int X { get; set; } }"), "brace.function").empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void F() { void L() { } L(); } }"), "brace.function")
                .size() == 2); // F's own body AND L's
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void F() { using (var x = G()) { H(); } } }"),
                          "brace.control")
                .size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void F() { lock (x) { H(); } } }"), "brace.control")
                .size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { void F() { unsafe { H(); } } }"), "brace.control").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { Action a = () => { G(); }; }"), "brace.function").size() ==
            1);
    // switch_expression has no "body:" field at all -- needs the paired
    // mechanism, same as a for-loop's own clause.
    const auto switchExpr =
        CapturesNamed(mode.formatCaptures("class C { void F() { var y = x switch { 1 => 2, _ => 3 }; } }"),
                      "brace.control");
    REQUIRE(switchExpr.size() == 1);
    REQUIRE_FALSE(switchExpr[0].isSimple); // no single node's span matches the synthesized range
}

TEST_CASE("javascript-mode's format.janet now covers method_definition (previously ZERO "
          "placement capture at all)/do-while/try's-own-body/finally/class-static-block/arrow-"
          "and-function-expression-bodies",
          "[FormatBracePlacement]") {
    const Mode mode = JavaScriptMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { f() { g(); } }"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { constructor() { g(); } }"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("const o = { f() { g(); } };"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function f() { do { g(); } while (x); }"), "brace.control").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function f() { do { g(); } while (x); }"), "control.parens").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("function f() { try { g(); } finally { h(); } }"), "brace.control")
                .size() == 2);
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { static { init(); } }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("const f = (x) => { return x; };"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("const f = function(x) { return x; };"), "brace.function").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("const f = function*(x) { yield x; };"), "brace.function").size() ==
            1);
    // Arrow functions can ALSO be bare-expression-bodied -- must not match.
    REQUIRE(CapturesNamed(mode.formatCaptures("const f = (x) => x + 1;"), "brace.function").empty());
}

TEST_CASE("typescript-mode's format.janet now covers enum bodies for brace.class",
          "[FormatBracePlacement]") {
    REQUIRE(CapturesNamed(TypeScriptMode().formatCaptures("enum E { A, B }"), "brace.class").size() == 1);
}

TEST_CASE("go-mode's format.janet now covers func_literal (anonymous function EXPRESSIONS, "
          "used constantly for goroutines/defer/callbacks) -- a real prior gap regardless of "
          "the anon-function policy question, not merely a policy-driven addition",
          "[FormatBracePlacement]") {
    const Mode mode = GoMode();
    const auto caps = CapturesNamed(mode.formatCaptures("func f() {\n\tgo func() {\n\t\th()\n\t}()\n}"),
                                    "brace.function");
    REQUIRE(caps.size() == 2); // f's own body AND the func_literal's
    REQUIRE(CapturesNamed(mode.formatCaptures("func f() {\n\tgo func() {\n\t\th()\n\t\ti()\n\t}()\n}"),
                          "brace.function")[1]
                .isSimple == false);
}

TEST_CASE("rust-mode's format.janet now covers closure_expression/unsafe_block/async_block/"
          "const_block",
          "[FormatBracePlacement]") {
    const Mode mode = RustMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("fn f() { let g = |x| { x }; }"), "brace.function").size() == 2);
    // A bare-expression-bodied closure must not match.
    REQUIRE(CapturesNamed(mode.formatCaptures("fn f() { let g = |x| x + 1; }"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("fn f() { unsafe { g(); } }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("fn f() { let x = async { g(); }; }"), "brace.control").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("const X: i32 = const { 1 + 1 };"), "brace.control").size() == 1);
}

TEST_CASE("php-mode's format.janet now covers closures (a real prior gap, extremely common/"
          "idiomatic in PHP)/anonymous-classes/match-expressions/enum-bodies",
          "[FormatBracePlacement]") {
    const Mode mode = PhpMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("<?php $f = function() { g(); };"), "brace.function").size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("<?php $c = new class { function f() {} };"), "brace.class").size() ==
            1);
    REQUIRE(CapturesNamed(mode.formatCaptures("<?php $y = match($x) { 1 => 2, default => 3 };"), "brace.control")
                .size() == 1);
    REQUIRE(CapturesNamed(mode.formatCaptures("<?php enum Suit { case Hearts; case Spades; }"), "brace.class")
                .size() == 1);
}

TEST_CASE("kotlin-mode's format.janet now covers do-while/init-blocks/secondary-constructors/"
          "object-literals/lambda-literals",
          "[FormatBracePlacement]") {
    const Mode mode = KotlinMode();
    REQUIRE(CapturesNamed(mode.formatCaptures("fun f() { do { g() } while (x) }"), "brace.control").size() == 1);
    // A brace-less do-while body must not match.
    REQUIRE(CapturesNamed(mode.formatCaptures("fun f() { do g() while (x) }"), "brace.control").empty());
    const auto init = CapturesNamed(mode.formatCaptures("class C { init { g() } }"), "brace.control");
    REQUIRE(init.size() == 1);
    REQUIRE_FALSE(init[0].isSimple); // no single node's span matches the synthesized "{".."}"
    REQUIRE(CapturesNamed(mode.formatCaptures("class C { constructor(x: Int) { g() } }"), "brace.function").size() ==
            1);
    // A bodyless delegating secondary constructor must not match.
    REQUIRE(CapturesNamed(mode.formatCaptures("class C(val y: Int) { constructor(x: Int) : this(x) }"),
                          "brace.function")
                .empty());
    REQUIRE(CapturesNamed(mode.formatCaptures("val o = object : Foo() { }"), "brace.class").size() == 1);
    // Trailing-lambda call syntax is a real, common lambda_literal use.
    const auto lambda = CapturesNamed(mode.formatCaptures("fun f() { list.map { it * 2 } }"), "brace.function");
    REQUIRE(lambda.size() == 2); // f's own body AND the trailing lambda
}
