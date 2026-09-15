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
using ned::editor::ComputeBracePlacementEdits;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::JavaScriptMode;
using ned::editor::Mode;
using ned::editor::SetBracePlacement;
using ned::text::Buffer;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetBracePlacement("brace.function", std::nullopt);
        SetBracePlacement("cpp/brace.function", std::nullopt);
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
    const std::vector<FormatCapture> captures = mode.formatCaptures(source);

    REQUIRE(captures.size() == 1);
    REQUIRE(captures[0].name == "brace.function");
    REQUIRE(captures[0].startByte == source.find('{'));
    REQUIRE(captures[0].endByte == source.size() - 1); // through the closing '}'
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
    const std::vector<FormatCapture> captures = mode.formatCaptures(source);

    REQUIRE(captures.size() == 1);
    REQUIRE(captures[0].name == "brace.function"); // same capture name as cpp's, different grammar node underneath
    REQUIRE(captures[0].startByte == source.find('{'));
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
    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].text == "\n    "); // no header indent (column 0) + one 4-space level
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
