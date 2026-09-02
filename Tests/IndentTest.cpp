#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Indent.h"
#include "Editor/IndentStyle.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::BashMode;
using ned::editor::ClojureMode;
using ned::editor::CMode;
using ned::editor::CppMode;
using ned::editor::CssMode;
using ned::editor::EffectiveIndentStyle;
using ned::editor::FishMode;
using ned::editor::FundamentalMode;
using ned::editor::HtmlMode;
using ned::editor::IndentBuffer;
using ned::editor::IndentRegion;
using ned::editor::IndentStyle;
using ned::editor::JanetMode;
using ned::editor::JankMode;
using ned::editor::JavaScriptMode;
using ned::editor::JsonMode;
using ned::editor::MarkdownMode;
using ned::editor::Mode;
using ned::editor::OrgMode;
using ned::editor::PhpMode;
using ned::editor::PythonMode;
using ned::editor::SetIndentStyleForMode;
using ned::editor::TomlMode;
using ned::editor::TsxMode;
using ned::editor::TypeScriptMode;
using ned::editor::XmlMode;
using ned::editor::YamlMode;
using ned::text::Buffer;

namespace {

// [lineStart, lineEnd) of a buffer's 0-indexed `line`'th line, excluding its
// own trailing newline -- mirrors IndentRegion's own internal computation,
// so a test case can just name a line by index.
std::pair<std::size_t, std::size_t> LineRange(const Buffer& buffer, std::size_t line) {
    const auto&       content    = buffer.Content();
    const std::size_t lineStart  = content.LineToByteOffset(line);
    std::size_t        lineEnd    = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
    if (line + 1 < content.LineCount() && lineEnd > lineStart) {
        --lineEnd;
    }
    return {lineStart, lineEnd};
}

} // namespace

TEST_CASE("CMode indentColumn indents inside a nested if-block", "[Indent]") {
    const auto mode = CMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.c");
    buffer.InsertAtPoint("int f(void) {\n    if (1) {\n        return 0;\n    }\n}\n");

    const auto [lineStart, lineEnd] = LineRange(buffer, 2); // "        return 0;"
    const auto column               = mode.indentColumn(buffer.Text(), lineStart, lineEnd);
    REQUIRE(column.has_value());
    REQUIRE(*column == 8); // two levels deep, width 4
}

TEST_CASE("CMode indentColumn aligns a closing brace with its opener's own level", "[Indent]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int f(void) {\n    if (1) {\n        return 0;\n    }\n}\n");

    const auto [lineStart, lineEnd] = LineRange(buffer, 3); // "    }" -- closes the if-block
    const auto column               = mode.indentColumn(buffer.Text(), lineStart, lineEnd);
    REQUIRE(column.has_value());
    REQUIRE(*column == 4); // matches "if (1) {"'s own level, not one deeper
}

TEST_CASE("CppMode indentColumn indents a struct member and a nested method body", "[Indent]") {
    const auto mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("struct S {\n    void f() {\n        return;\n    }\n};\n");

    const auto [memberStart, memberEnd] = LineRange(buffer, 1); // "    void f() {"
    const auto memberColumn             = mode.indentColumn(buffer.Text(), memberStart, memberEnd);
    REQUIRE(memberColumn.has_value());
    REQUIRE(*memberColumn == 4);

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "        return;"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 8);
}

TEST_CASE("JsonMode indentColumn indents a nested array element and aligns its closing bracket", "[Indent]") {
    const auto mode = JsonMode();
    Buffer     buffer("test.json");
    buffer.InsertAtPoint("{\n\"a\": [\n1\n]\n}\n");

    const auto [elementStart, elementEnd] = LineRange(buffer, 2); // "1"
    const auto elementColumn              = mode.indentColumn(buffer.Text(), elementStart, elementEnd);
    REQUIRE(elementColumn.has_value());
    REQUIRE(*elementColumn == 8); // two levels deep (object + array), width 4

    const auto [closeStart, closeEnd] = LineRange(buffer, 3); // "]"
    const auto closeColumn            = mode.indentColumn(buffer.Text(), closeStart, closeEnd);
    REQUIRE(closeColumn.has_value());
    REQUIRE(*closeColumn == 4); // matches "\"a\": ["'s own level
}

TEST_CASE("PythonMode indentColumn indents a function body and a nested if-block", "[Indent]") {
    const auto mode = PythonMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.py");
    buffer.InsertAtPoint("def f():\n    if x:\n        return 1\n");

    const auto [ifStart, ifEnd] = LineRange(buffer, 1); // "    if x:"
    const auto ifColumn         = mode.indentColumn(buffer.Text(), ifStart, ifEnd);
    REQUIRE(ifColumn.has_value());
    REQUIRE(*ifColumn == 4);

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "        return 1"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 8);
}

TEST_CASE("PythonMode indentColumn aligns an else clause with its owning if, not the if-block's body", "[Indent]") {
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("def f():\n    if x:\n        return 1\n    else:\n        return 2\n");

    const auto [elseStart, elseEnd] = LineRange(buffer, 3); // "    else:"
    const auto elseColumn           = mode.indentColumn(buffer.Text(), elseStart, elseEnd);
    REQUIRE(elseColumn.has_value());
    REQUIRE(*elseColumn == 4); // matches "if x:"'s own level, not "return 1"'s

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 4); // "        return 2"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 8);
}

TEST_CASE("PythonMode indentColumn end-of-block dedent needs no explicit dedent capture", "[Indent]") {
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    // A line after the function entirely -- no enclosing block at all.
    buffer.InsertAtPoint("def f():\n    return 1\nx = 2\n");

    const auto [afterStart, afterEnd] = LineRange(buffer, 2); // "x = 2"
    const auto afterColumn            = mode.indentColumn(buffer.Text(), afterStart, afterEnd);
    REQUIRE(afterColumn.has_value());
    REQUIRE(*afterColumn == 0);
}

TEST_CASE("PythonMode indentColumn indents a multi-line call's continuation line", "[Indent]") {
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("f(a,\nb)\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "b)"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 4);
}

TEST_CASE("PythonMode indentColumn aligns a lone closing paren with its call's own opening line", "[Indent]") {
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("f(a,\nb\n)\n");

    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // ")"
    const auto closeColumn            = mode.indentColumn(buffer.Text(), closeStart, closeEnd);
    REQUIRE(closeColumn.has_value());
    REQUIRE(*closeColumn == 0); // aligns with "f(a,"'s own level, not "b"'s
}

TEST_CASE("MarkdownMode indentColumn hangs a nested list item's continuation to its own marker width",
          "[Indent]") {
    const auto mode = MarkdownMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.md");
    buffer.InsertAtPoint("- item one\n  more text\n");

    const auto [markerStart, markerEnd] = LineRange(buffer, 0); // "- item one" -- its own marker line
    const auto markerColumn             = mode.indentColumn(buffer.Text(), markerStart, markerEnd);
    REQUIRE(markerColumn.has_value());
    REQUIRE(*markerColumn == 0);

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "  more text" -- hanging continuation
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 2); // "- " is 2 columns wide
}

TEST_CASE("MarkdownMode indentColumn stacks nested list markers additively", "[Indent]") {
    const auto mode = MarkdownMode();
    Buffer     buffer("test.md");
    buffer.InsertAtPoint("1. outer\n   - inner\n     more\n");

    const auto [innerMarkerStart, innerMarkerEnd] = LineRange(buffer, 1); // "   - inner" -- inner item's own marker line
    const auto innerMarkerColumn                  = mode.indentColumn(buffer.Text(), innerMarkerStart, innerMarkerEnd);
    REQUIRE(innerMarkerColumn.has_value());
    REQUIRE(*innerMarkerColumn == 3); // "1. " is 3 columns -- the outer item's own contribution only

    const auto [contStart, contEnd] = LineRange(buffer, 2); // "     more" -- inside the inner item's body
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 5); // "1. " (3) + "- " (2)
}

TEST_CASE("MarkdownMode indentColumn adds 2 columns per blockquote level", "[Indent]") {
    const auto mode = MarkdownMode();
    Buffer     buffer("test.md");
    buffer.InsertAtPoint("> quoted\n> more quoted\n");

    const auto [firstStart, firstEnd] = LineRange(buffer, 0); // "> quoted" -- the blockquote's own opening line
    const auto firstColumn            = mode.indentColumn(buffer.Text(), firstStart, firstEnd);
    REQUIRE(firstColumn.has_value());
    REQUIRE(*firstColumn == 0);

    const auto [secondStart, secondEnd] = LineRange(buffer, 1); // "> more quoted" -- still inside the same blockquote
    const auto secondColumn             = mode.indentColumn(buffer.Text(), secondStart, secondEnd);
    REQUIRE(secondColumn.has_value());
    REQUIRE(*secondColumn == 2);
}

TEST_CASE("MarkdownMode indentColumn copies a fenced code block's own content indentation verbatim", "[Indent]") {
    const auto mode = MarkdownMode();
    Buffer     buffer("test.md");
    buffer.InsertAtPoint("```\n    weird indent\nnext line\n```\n");

    // The line right after one with deliberately "wrong"/non-structural
    // indentation -- passthrough copies it as-is, not recomputed.
    const auto [nextStart, nextEnd] = LineRange(buffer, 2); // "next line"
    const auto nextColumn           = mode.indentColumn(buffer.Text(), nextStart, nextEnd);
    REQUIRE(nextColumn.has_value());
    REQUIRE(*nextColumn == 4); // copies "    weird indent"'s own 4-space leading run
}

TEST_CASE("JavaScriptMode indentColumn indents a nested if-block and aligns its closing brace", "[Indent]") {
    const auto mode = JavaScriptMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.js");
    buffer.InsertAtPoint("function f() {\nif (x) {\nreturn 1;\n}\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "return 1;"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 8);

    const auto [closeStart, closeEnd] = LineRange(buffer, 3); // "}" closing the if
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 4);
}

TEST_CASE("TypeScriptMode indentColumn indents an interface body", "[Indent]") {
    const auto mode = TypeScriptMode();
    Buffer     buffer("test.ts");
    buffer.InsertAtPoint("interface I {\nx: number;\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "x: number;"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "}"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("TsxMode indentColumn shares TypeScript's own statement_block indentation", "[Indent]") {
    const auto mode = TsxMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.tsx");
    buffer.InsertAtPoint("function f() {\nreturn 1;\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1);
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
}

TEST_CASE("PhpMode indentColumn indents an if-block and aligns its closing brace", "[Indent]") {
    const auto mode = PhpMode();
    Buffer     buffer("test.php");
    buffer.InsertAtPoint("<?php\nif ($x) {\necho 1;\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "echo 1;"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 3); // "}"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("CssMode indentColumn indents a rule body and aligns its closing brace", "[Indent]") {
    const auto mode = CssMode();
    Buffer     buffer("test.css");
    buffer.InsertAtPoint(".a {\ncolor: red;\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "color: red;"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "}"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("HtmlMode indentColumn indents a nested element and aligns its closing tag", "[Indent]") {
    const auto mode = HtmlMode();
    Buffer     buffer("test.html");
    buffer.InsertAtPoint("<div>\n<p>hi</p>\n</div>\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "<p>hi</p>"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "</div>"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("XmlMode indentColumn indents a nested element and aligns its closing tag", "[Indent]") {
    const auto mode = XmlMode();
    Buffer     buffer("test.xml");
    buffer.InsertAtPoint("<a>\n<b>x</b>\n</a>\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "<b>x</b>"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "</a>"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("BashMode indentColumn indents an if-body and aligns fi with its own if", "[Indent]") {
    const auto mode = BashMode();
    Buffer     buffer("test.sh");
    buffer.InsertAtPoint("if x; then\necho y\nfi\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "echo y"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "fi"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("BashMode indentColumn indents a for-loop body via do_group and aligns done", "[Indent]") {
    const auto mode = BashMode();
    Buffer     buffer("test.sh");
    buffer.InsertAtPoint("for i in a b; do\necho $i\ndone\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "echo $i"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "done"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("FishMode indentColumn indents an if-body and aligns end with its own if", "[Indent]") {
    const auto mode = FishMode();
    Buffer     buffer("test.fish");
    buffer.InsertAtPoint("if test 1\necho a\nend\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "echo a"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "end"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("JanetMode indentColumn indents inside a form and aligns a lone closing paren", "[Indent]") {
    const auto mode = JanetMode();
    Buffer     buffer("test.janet");
    buffer.InsertAtPoint("(a\nb\n)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "b"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // ")"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("ClojureMode indentColumn indents inside a form and aligns a lone closing paren", "[Indent]") {
    const auto mode = ClojureMode();
    Buffer     buffer("test.clj");
    buffer.InsertAtPoint("(a\nb\n)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "b"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // ")"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("JankMode indentColumn shares Clojure's own indentation", "[Indent]") {
    const auto mode = JankMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.jank");
    buffer.InsertAtPoint("(a\nb)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "b)"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
}

TEST_CASE("YamlMode indentColumn indents one level per genuinely nested mapping, not the document root",
          "[Indent]") {
    const auto mode = YamlMode();
    Buffer     buffer("test.yaml");
    buffer.InsertAtPoint("a:\n  b:\n    c: 1\n");

    const auto [level1Start, level1End] = LineRange(buffer, 1); // "  b:"
    REQUIRE(mode.indentColumn(buffer.Text(), level1Start, level1End) == 4);
    const auto [level2Start, level2End] = LineRange(buffer, 2); // "    c: 1"
    REQUIRE(mode.indentColumn(buffer.Text(), level2Start, level2End) == 8);
}

TEST_CASE("YamlMode indentColumn indents a nested sequence item", "[Indent]") {
    const auto mode = YamlMode();
    Buffer     buffer("test.yaml");
    buffer.InsertAtPoint("a:\n  - x\n  - y\n");

    const auto [itemStart, itemEnd] = LineRange(buffer, 1); // "  - x"
    REQUIRE(mode.indentColumn(buffer.Text(), itemStart, itemEnd) == 4);
}

TEST_CASE("TomlMode indentColumn indents inside a multi-line array and aligns its closing bracket", "[Indent]") {
    const auto mode = TomlMode();
    Buffer     buffer("test.toml");
    buffer.InsertAtPoint("a = [\n1,\n2\n]\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "1,"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    const auto [closeStart, closeEnd] = LineRange(buffer, 3); // "]"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("OrgMode indentColumn hangs a list item's continuation to its own bullet width", "[Indent]") {
    const auto mode = OrgMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.org");
    buffer.InsertAtPoint("- item one\n  more text\n");

    const auto [bulletStart, bulletEnd] = LineRange(buffer, 0); // "- item one" -- its own bullet line
    REQUIRE(mode.indentColumn(buffer.Text(), bulletStart, bulletEnd) == 0);

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "  more text" -- hanging continuation
    REQUIRE(mode.indentColumn(buffer.Text(), contStart, contEnd) == 2); // "- " is 2 columns wide
}

TEST_CASE("FundamentalMode has no indentColumn configured", "[Indent]") {
    const Mode mode = FundamentalMode();
    REQUIRE_FALSE(static_cast<bool>(mode.indentColumn));
}

TEST_CASE("IndentRegion/IndentBuffer reindent a deliberately misindented C file as one undo step", "[Indent]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    // Deliberately wrong existing indentation throughout.
    buffer.InsertAtPoint("int f(void) {\nif (1) {\n           return 0;\n}\n}\n");

    const std::size_t changed = IndentBuffer(buffer, mode);
    REQUIRE(changed > 0);

    REQUIRE(buffer.Text() == "int f(void) {\n    if (1) {\n        return 0;\n    }\n}\n");

    REQUIRE(buffer.CanUndo());
    buffer.Undo();
    REQUIRE(buffer.Text() == "int f(void) {\nif (1) {\n           return 0;\n}\n}\n");
}

TEST_CASE("IndentBuffer is a no-op for a mode with no indentColumn configured", "[Indent]") {
    const Mode mode = FundamentalMode();
    Buffer     buffer("test.txt");
    buffer.InsertAtPoint("anything\n  at all\n");
    const std::string before = buffer.Text();

    REQUIRE(IndentBuffer(buffer, mode) == 0);
    REQUIRE(buffer.Text() == before);
}

TEST_CASE("IndentRegion respects a per-mode indent style override", "[Indent]") {
    SetIndentStyleForMode("c-mode", IndentStyle{.useTabs = false, .width = 2});
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int f(void) {\nreturn 0;\n}\n");

    REQUIRE(IndentBuffer(buffer, mode) > 0);
    REQUIRE(buffer.Text() == "int f(void) {\n  return 0;\n}\n");

    // Restore the default for any later test relying on the usual width-4
    // process-wide default (IndentStyle.h's own state is process-wide).
    SetIndentStyleForMode("c-mode", IndentStyle{.useTabs = false, .width = 4});
}
