#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Editor/HugeStructuralWindow.h"
#include "Editor/Indent.h"
#include "Editor/IndentStyle.h"
#include "Editor/Mode.h"
#include "Editor/TabWidth.h"
#include "Text/Buffer.h"

using ned::editor::BashMode;
using ned::editor::ClojureMode;
using ned::editor::CMode;
using ned::editor::CppMode;
using ned::editor::CSharpMode;
using ned::editor::CssMode;
using ned::editor::EffectiveIndentStyle;
using ned::editor::FishMode;
using ned::editor::FundamentalMode;
using ned::editor::GoMode;
using ned::editor::HtmlMode;
using ned::editor::IndentBuffer;
using ned::editor::IndentRegion;
using ned::editor::IndentStyle;
using ned::editor::JanetMode;
using ned::editor::JankMode;
using ned::editor::JavaMode;
using ned::editor::JavaScriptMode;
using ned::editor::JsonMode;
using ned::editor::KotlinMode;
using ned::editor::MarkdownMode;
using ned::editor::Mode;
using ned::editor::OrgMode;
using ned::editor::PhpMode;
using ned::editor::PythonMode;
using ned::editor::RigidShiftRegion;
using ned::editor::RubyMode;
using ned::editor::RustMode;
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
    const auto&       content   = buffer.Content();
    const std::size_t lineStart = content.LineToByteOffset(line);
    std::size_t       lineEnd   = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
    if (line + 1 < content.LineCount() && lineEnd > lineStart) {
        --lineEnd;
    }
    return {lineStart, lineEnd};
}

// huge-file-indent-windowing follow-up: mirrors
// BufferViewHugeStructuralGutterTest.cpp's own WriteTempFile/FromHugeFile
// precedent -- Buffer::FromHugeFile doesn't itself check size (only
// BufferList::OpenFile's threshold gate does), so a small file loaded this
// way still reports ITextStorage::IsHuge() == true and exercises
// IndentRegion/IndentBuffer's real windowed path without needing an
// actually huge file on disk.
std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream               file(path, std::ios::binary);
    file << content;
    return path;
}

// Process-wide state -- restored via RAII, matching
// BufferViewHugeStructuralGutterTest.cpp's own HugeStructuralWindowBytesGuard.
struct HugeStructuralWindowBytesGuard {
    ~HugeStructuralWindowBytesGuard() {
        ned::editor::SetHugeStructuralWindowBytes(4 * 1024 * 1024);
    }
};

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

TEST_CASE("CMode indentColumn aligns a wrapped call's continuation argument to the first argument's column",
          "[Indent]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int r = foo(a,\n            b);\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "            b);"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 12); // aligns under "a", the byte right after "("
}

TEST_CASE("CMode indentColumn falls back to a plain indent level when a wrapped call's opener is alone on its line",
          "[Indent]") {
    const auto mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int r = foo(\n    a\n);\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "    a"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 4); // nothing to align to -- one ordinary indent level
}

TEST_CASE("GoMode indentColumn indents inside a nested if-block and aligns its closing brace", "[Indent]") {
    const auto mode = GoMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.go");
    buffer.InsertAtPoint("func f() {\n    if true {\n        return\n    }\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "        return"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 8); // two levels deep, width 4

    const auto [closeStart, closeEnd] = LineRange(buffer, 3); // "    }" -- closes the if-block
    const auto closeColumn            = mode.indentColumn(buffer.Text(), closeStart, closeEnd);
    REQUIRE(closeColumn.has_value());
    REQUIRE(*closeColumn == 4); // matches "if true {"'s own level, not one deeper
}

TEST_CASE("GoMode indentColumn aligns a wrapped call's continuation argument to the first argument's column",
          "[Indent]") {
    const auto mode = GoMode();
    Buffer     buffer("test.go");
    buffer.InsertAtPoint("var r = foo(a,\n            b)\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "            b)"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 12); // aligns under "a", the byte right after "("
}

TEST_CASE("CSharpMode indentColumn indents a nested if-block and aligns its closing brace", "[Indent]") {
    const auto mode = CSharpMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.cs");
    buffer.InsertAtPoint("class C {\n    void F() {\n        if (true) {\n            return;\n        }\n    }\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 3); // "            return;"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 12); // three levels deep, width 4

    const auto [closeStart, closeEnd] = LineRange(buffer, 4); // "        }" -- closes the if-block
    const auto closeColumn            = mode.indentColumn(buffer.Text(), closeStart, closeEnd);
    REQUIRE(closeColumn.has_value());
    REQUIRE(*closeColumn == 8); // matches "if (true) {"'s own level, not one deeper
}

TEST_CASE("CSharpMode indentColumn aligns a wrapped call's continuation argument to the first argument's column",
          "[Indent]") {
    const auto mode = CSharpMode();
    Buffer     buffer("test.cs");
    buffer.InsertAtPoint("var r = Foo(a,\n            b);\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "            b);"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 12); // aligns under "a", the byte right after "("
}

TEST_CASE("JavaMode indentColumn indents a nested if-block and aligns its closing brace", "[Indent]") {
    const auto mode = JavaMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("Test.java");
    buffer.InsertAtPoint("class C {\n    void f() {\n        if (true) {\n            return;\n        }\n    }\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 3); // "            return;"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 12); // three levels deep, width 4

    const auto [closeStart, closeEnd] = LineRange(buffer, 4); // "        }" -- closes the if-block
    const auto closeColumn            = mode.indentColumn(buffer.Text(), closeStart, closeEnd);
    REQUIRE(closeColumn.has_value());
    REQUIRE(*closeColumn == 8); // matches "if (true) {"'s own level, not one deeper
}

TEST_CASE("JavaMode indentColumn aligns a wrapped call's continuation argument to the first argument's column",
          "[Indent]") {
    const auto mode = JavaMode();
    Buffer     buffer("Test.java");
    buffer.InsertAtPoint("class C {\n    void f() {\n        var r = foo(a,\n                    b);\n    }\n}\n");

    const auto [contStart, contEnd] = LineRange(buffer, 3); // "                    b);"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 20); // aligns under "a", the byte right after "("
}

TEST_CASE("KotlinMode indentColumn indents a nested braced body and aligns its closing brace", "[Indent]") {
    const auto mode = KotlinMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("Test.kt");
    buffer.InsertAtPoint("class C {\n    fun f() {\n        if (true) {\n            return\n        }\n    }\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 3); // "            return"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 12); // three levels deep, width 4

    const auto [closeStart, closeEnd] = LineRange(buffer, 4); // "        }" -- closes the if-branch
    const auto closeColumn            = mode.indentColumn(buffer.Text(), closeStart, closeEnd);
    REQUIRE(closeColumn.has_value());
    REQUIRE(*closeColumn == 8);
}

TEST_CASE("KotlinMode indentColumn aligns a wrapped call's continuation argument to the first argument's column",
          "[Indent]") {
    const auto mode = KotlinMode();
    Buffer     buffer("Test.kt");
    buffer.InsertAtPoint("val r = foo(a,\n            b)\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "            b)"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 12); // aligns under "a", the byte right after "("
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

// indent-cache-by-byte-range follow-up: the actual property BuildIndentFunction's
// MatchCache wiring exists for -- calling the SAME Mode instance's indentColumn
// repeatedly across an evolving sequence of edits (sharing one MatchCache under
// the hood, reconciled incrementally) must report the exact same thing a
// completely FRESH Mode would report on that exact text, at every step. Each
// step queries the blank line just opened by a real Enter press (lineStart ==
// lineEnd == text.size(), Mode.h's own convention for that), which exercises
// the walk's own end-of-buffer rescue path too.
TEST_CASE("CppMode's indentColumn stays correct across a sequence of incremental edits", "[Indent]") {
    const auto mode = CppMode();
    REQUIRE(static_cast<bool>(mode.indentColumn));

    const std::vector<std::string> steps = {
        "class Widget {\npublic:\n    void run() {\n",
        "class Widget {\npublic:\n    void run() {\n        int x = 1;\n",
        "class Widget {\npublic:\n    void run() {\n        int x = 1;\n        if (x) {\n",
        "class Widget {\npublic:\n    void run() {\n        int x = 1;\n        if (x) {\n            x++;\n",
        "class Widget {\npublic:\n    void run() {\n        int x = 1;\n        if (x) {\n            x++;\n        }\n",
        "class Widget {\npublic:\n    void run() {\n        int x = 1;\n        if (x) {\n            x++;\n        }\n"
        "    }\n",
        "class Widget {\npublic:\n    void run() {\n        int x = 1;\n        if (x) {\n            x++;\n        }\n"
        "    }\n};\n",
    };

    bool sawRealValue = false;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const std::string& text      = steps[i];
        const std::size_t  lineStart = text.size();
        const std::size_t  lineEnd   = text.size();
        INFO("step " << i << ": " << text);
        const auto incremental = mode.indentColumn(text, lineStart, lineEnd);
        const auto fresh       = CppMode().indentColumn(text, lineStart, lineEnd);
        REQUIRE(incremental == fresh);
        sawRealValue = sawRealValue || incremental.has_value();
    }
    REQUIRE(sawRealValue); // not vacuously comparing nullopt against nullopt throughout
}

// per-subtree-fact-memoization follow-up: a harder variant of "stays correct
// across a sequence of incremental edits" above -- that test calls
// indentColumn on EVERY step, which never actually exercises indentMatchCache
// falling behind sharedParse's own generation. Here indentColumn is called
// only on even steps; odd steps call ONLY mode.highlight (sharedParse's own
// per-Paint()-cadence capability), advancing the shared IncrementalParseCache
// to a text indentMatchCache never reconciled against -- exactly the shape
// a real "several ordinary keystrokes between two Enter presses" sequence
// has. Each step also inserts its new line BEFORE the call (not appended
// after it), so a coordinate-shift bug reconciling a stale cached_ against
// an edit that doesn't describe its own true baseline-to-current delta would
// have real byte offsets to get wrong, not a no-op append. It passes because
// MatchCache's own ambiguity-reclassification structural-widening check
// (comparing the edit's old-vs-new enclosing named node) detects the
// resulting nonsense old-frame lookup and widens the redo window to cover
// the whole affected region, forcing a fresh re-derive rather than trusting
// a wrongly-shifted stale entry -- confirmed by instrumenting Reconcile()
// directly during development of this test, not assumed from the source
// reading alone. Uses JavaScript's lambda-body-alignment @align.barrier
// (Source/Languages/javascript/indents.janet) as the probed fact: querying
// the callback body's own statement makes the answer depend on
// "statement_block" actually being found in the align.barrier set, unlike a
// plain brace-nesting case where the dedent comes fresh from the delimiter
// imprint every call regardless of MatchCache's own staleness.
TEST_CASE("JavaScriptMode's indentColumn stays correct when called sporadically, skipping generations "
          "highlight alone advanced",
          "[Indent]") {
    const auto mode = JavaScriptMode();
    REQUIRE(static_cast<bool>(mode.indentColumn));
    REQUIRE(static_cast<bool>(mode.highlight));

    // Each step prepends one more "// padN" line BEFORE the call, shifting
    // every byte of the call (and its cached aligned/align.barrier captures)
    // forward -- unlike appending after them, which a single edit's shift
    // handles correctly regardless of how many generations were skipped.
    // Query point: the callback body's own statement -- its correct indent
    // depends on "statement_block" successfully being found in the
    // align.barrier set (JS's own lambda-body-alignment rule degrading the
    // OUTER "arguments" @aligned container back to plain level counting);
    // a coordinate-corrupted/missing align.barrier entry would instead align
    // this line to the column right after "setTimeout(".
    std::vector<std::string> steps;
    {
        std::string prefix;
        for (int i = 0; i < 6; ++i) {
            prefix += "// pad" + std::to_string(i) + "\n";
            steps.push_back(prefix + "setTimeout(function() {\n  a();\n}, 100);\n");
        }
    }

    auto queryPoint = [](const std::string& text) {
        const std::size_t bodyLine = text.find("  a();");
        REQUIRE(bodyLine != std::string::npos);
        return bodyLine;
    };
    {
        const std::size_t q = queryPoint(steps[0]);
        const auto        c = mode.indentColumn(steps[0], q, q);
        const auto        f = JavaScriptMode().indentColumn(steps[0], q, q);
        REQUIRE(c == f);
    }

    for (std::size_t i = 1; i < steps.size(); ++i) {
        if (i % 2 == 1) {
            // Odd steps: simulate ordinary typing -- only highlight observes
            // this generation, indentColumn does not.
            (void)mode.highlight(steps[i], ned::editor::HighlightWindow{});
            continue;
        }
        // Even steps: a real Enter/reindent request, skipping the odd
        // generation indentMatchCache never saw.
        const std::size_t q = queryPoint(steps[i]);
        INFO("step " << i);
        const auto incremental = mode.indentColumn(steps[i], q, q);
        const auto fresh       = JavaScriptMode().indentColumn(steps[i], q, q);
        INFO("incremental = " << (incremental ? std::to_string(*incremental) : "nullopt"));
        INFO("fresh       = " << (fresh ? std::to_string(*fresh) : "nullopt"));
        REQUIRE(incremental == fresh);
    }
}

TEST_CASE("JsonMode indentColumn indents a nested array element and aligns its closing bracket", "[Indent]") {
    const auto mode = JsonMode();
    Buffer     buffer("test.json");
    buffer.InsertAtPoint("{\n\"a\": [\n1\n]\n}\n");

    const auto [elementStart, elementEnd] = LineRange(buffer, 2); // "1"
    const auto elementColumn              = mode.indentColumn(buffer.Text(), elementStart, elementEnd);
    REQUIRE(elementColumn.has_value());
    REQUIRE(*elementColumn == 4); // two levels deep (object + array), width 2 (json's own built-in default -- IndentDefaults.h)

    const auto [closeStart, closeEnd] = LineRange(buffer, 3); // "]"
    const auto closeColumn            = mode.indentColumn(buffer.Text(), closeStart, closeEnd);
    REQUIRE(closeColumn.has_value());
    REQUIRE(*closeColumn == 2); // matches "\"a\": ["'s own level
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

TEST_CASE("PythonMode indentColumn takes a following container's level for a comment as the "
          "first line of that container's body, not the header's own level",
          "[Indent]") {
    // A comment immediately under "def f():" attaches to the grammar as a
    // SIBLING of "block" (one level short of it), not as a child inside
    // it -- Python's "block" node has no opening delimiter of its own to
    // capture, so its range starts at its first real statement, one byte
    // past where the comment ends (confirmed via a real parse dump). A
    // naive ancestor walk from the comment therefore never counts "block"
    // at all and resolves it to column 0, matching neither the header nor
    // the body it visually precedes. Exactly Tests/Oracle/corpus/
    // sample.py's own "commented()" function.
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("def commented():\n    # a leading comment\n    return 1\n");

    const auto [commentStart, commentEnd] = LineRange(buffer, 1); // "    # a leading comment"
    const auto commentColumn              = mode.indentColumn(buffer.Text(), commentStart, commentEnd);
    REQUIRE(commentColumn.has_value());
    REQUIRE(*commentColumn == 4); // matches "return 1"'s own level, not "def"'s

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "    return 1"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 4);
}

TEST_CASE("PythonMode indentColumn takes a following container's level for a comment as the "
          "first line of a nested if-block's body, two containers deep",
          "[Indent]") {
    // Same shape as the top-level case above, but nested inside an
    // if-block too -- proves the fix isn't a Level==0-only rescue: a
    // comment's naive walk here resolves to level 1 (only the outer
    // function body counted; the if's own consequence "block", which
    // starts after the comment, is never an ancestor of it), one level
    // short of the correct 2, not 0.
    const auto mode = PythonMode();
    Buffer     buffer("test.py");
    buffer.InsertAtPoint("def f():\n    if True:\n        # a leading comment\n        x = 1\n");

    const auto [commentStart, commentEnd] = LineRange(buffer, 2); // "        # a leading comment"
    const auto commentColumn              = mode.indentColumn(buffer.Text(), commentStart, commentEnd);
    REQUIRE(commentColumn.has_value());
    REQUIRE(*commentColumn == 8); // matches "x = 1"'s own level

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 3); // "        x = 1"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 8);
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

TEST_CASE("MarkdownMode indentColumn hangs a nested list item's continuation one indent step in, "
          "not the marker's own literal width",
          "[Indent]") {
    // checkbox-hang-matches-tab-depth follow-up: one configured step
    // (EffectiveIndentStyle's own width, 4 by default), not "- "'s own
    // literal 2-column width -- matches the same document-wide convention
    // the wrap-indent-hang fix already established for the VISUAL
    // soft-wrap case.
    const auto mode = MarkdownMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.md");
    buffer.InsertAtPoint("- item one\n    more text\n");

    const auto [markerStart, markerEnd] = LineRange(buffer, 0); // "- item one" -- its own marker line
    const auto markerColumn             = mode.indentColumn(buffer.Text(), markerStart, markerEnd);
    REQUIRE(markerColumn.has_value());
    REQUIRE(*markerColumn == 0);

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "    more text" -- hanging continuation
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 4);
}

TEST_CASE("MarkdownMode indentColumn stacks one indent step per nesting level, additively", "[Indent]") {
    // Written indentation matches the marker's own literal width ("1. " is
    // 3, "- " is 2), not the new computed step (4) -- what's on the line
    // only needs to be enough for tree-sitter-markdown to recognize the
    // real nesting relationship; indentColumn recomputes independently of
    // whatever's literally written, and using the old widths here keeps
    // this test's own buffer setup on grammar-verified-correct ground.
    const auto mode = MarkdownMode();
    Buffer     buffer("test.md");
    buffer.InsertAtPoint("1. outer\n   - inner\n     more\n");

    const auto [innerMarkerStart, innerMarkerEnd] = LineRange(buffer, 1); // "   - inner" -- inner item's own marker line
    const auto innerMarkerColumn                  = mode.indentColumn(buffer.Text(), innerMarkerStart, innerMarkerEnd);
    REQUIRE(innerMarkerColumn.has_value());
    REQUIRE(*innerMarkerColumn == 4); // one step -- the outer item's own contribution only

    const auto [contStart, contEnd] = LineRange(buffer, 2); // "     more" -- inside the inner item's body
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 8); // two steps, one per nesting level
}

TEST_CASE("MarkdownMode indentColumn adds one indent step per blockquote level", "[Indent]") {
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
    REQUIRE(*secondColumn == 4);
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

TEST_CASE("MarkdownMode indentColumn breaks out of a list on a second consecutive blank Enter", "[Indent]") {
    // smart-blank-line-on-newline follow-up. "- item one\n  \n" already has
    // ONE auto-indented blank continuation line (2 spaces, matching "- "'s
    // own width) -- querying for a brand new blank line right after it
    // (the lineStart == lineEnd convention "newline" itself uses) is what a
    // SECOND consecutive Enter looks like.
    const auto mode = MarkdownMode();
    Buffer     buffer("test.md");
    buffer.InsertAtPoint("- item one\n  \n");

    const std::size_t newLinePos = buffer.Content().ByteLength();
    const auto        column     = mode.indentColumn(buffer.Text(), newLinePos, newLinePos);
    REQUIRE(column.has_value());
    REQUIRE(*column == 0);
}

TEST_CASE("MarkdownMode indentColumn still hangs a blank continuation on the FIRST Enter (single blank line)",
          "[Indent]") {
    // Same shape as the "breaks out" test above, but only ONE real content
    // line precedes -- confirms the second-blank-line check doesn't
    // misfire on the ordinary, ubiquitous single-Enter case.
    const auto mode = MarkdownMode();
    Buffer     buffer("test.md");
    buffer.InsertAtPoint("- item one\n");

    const std::size_t newLinePos = buffer.Content().ByteLength();
    const auto        column     = mode.indentColumn(buffer.Text(), newLinePos, newLinePos);
    REQUIRE(column.has_value());
    REQUIRE(*column == 4);
}

TEST_CASE("JavaScriptMode indentColumn indents a nested if-block and aligns its closing brace", "[Indent]") {
    const auto mode = JavaScriptMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.js");
    buffer.InsertAtPoint("function f() {\nif (x) {\nreturn 1;\n}\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "return 1;"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4); // two levels deep, javascript's own built-in default is width 2 (IndentDefaults.h)

    const auto [closeStart, closeEnd] = LineRange(buffer, 3); // "}" closing the if
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 2);
}

TEST_CASE("JavaScriptMode indentColumn aligns a wrapped call's continuation argument to the first argument's column",
          "[Indent]") {
    const auto mode = JavaScriptMode();
    Buffer     buffer("test.js");
    buffer.InsertAtPoint("foo(a,\n    b);\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "    b);"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 4); // aligns under "a", the byte right after "("
}

TEST_CASE("TypeScriptMode indentColumn indents an interface body", "[Indent]") {
    const auto mode = TypeScriptMode();
    Buffer     buffer("test.ts");
    buffer.InsertAtPoint("interface I {\nx: number;\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "x: number;"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // typescript's own built-in default is width 2 (IndentDefaults.h)
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "}"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("TsxMode indentColumn shares TypeScript's own statement_block indentation", "[Indent]") {
    const auto mode = TsxMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.tsx");
    buffer.InsertAtPoint("function f() {\nreturn 1;\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1);
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // tsx's own built-in default is width 2 (IndentDefaults.h)
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
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // css's own built-in default is width 2 (IndentDefaults.h)
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "}"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("HtmlMode indentColumn indents a nested element and aligns its closing tag", "[Indent]") {
    const auto mode = HtmlMode();
    Buffer     buffer("test.html");
    buffer.InsertAtPoint("<div>\n<p>hi</p>\n</div>\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "<p>hi</p>"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // html's own built-in default is width 2 (IndentDefaults.h)
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "</div>"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

// The state a file is in for most of its life: being typed, with the
// constructs above the cursor not closed yet. That used to park the parse's
// whole stack in one flat ERROR node (nothing @indent-captured left to walk),
// so every line in the file answered column 0 -- and indent-buffer/on-save
// format did not merely fail to indent such a file, it stripped the
// indentation already in it. Engine::CloseOpenConstructsAtEof is what keeps
// the nesting readable here; these pin the symptom rather than the tree.
TEST_CASE("indentColumn keeps indenting while a document is still unfinished", "[Indent]") {
    SECTION("html, two tags still open") {
        const auto mode = HtmlMode();
        Buffer     buffer("test.html");
        buffer.InsertAtPoint("<div>\n<ul>\n<li>one</li>\n");

        const auto [ulStart, ulEnd] = LineRange(buffer, 1); // "<ul>"
        REQUIRE(mode.indentColumn(buffer.Text(), ulStart, ulEnd) == 2);
        const auto [liStart, liEnd] = LineRange(buffer, 2); // "<li>one</li>"
        REQUIRE(mode.indentColumn(buffer.Text(), liStart, liEnd) == 4);
    }

    SECTION("c++, two blocks still open") {
        const auto mode = CppMode();
        Buffer     buffer("test.cpp");
        buffer.InsertAtPoint("struct Widget {\nvoid resize(int w) {\nwidth_ = w;\n");

        const auto [signatureStart, signatureEnd] = LineRange(buffer, 1); // "void resize(int w) {"
        REQUIRE(mode.indentColumn(buffer.Text(), signatureStart, signatureEnd) == 4);
        const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "width_ = w;"
        REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 8);
    }

    SECTION("rust, one block still open") {
        const auto mode = RustMode();
        Buffer     buffer("test.rs");
        buffer.InsertAtPoint("fn first() -> i32 {\nlet x = 1;\n");

        const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "let x = 1;"
        REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 4);
    }
}

// The destructive half of the same bug: a whole-buffer reindent of an
// unfinished file must leave its existing indentation alone rather than
// flatten it.
TEST_CASE("IndentBuffer does not flatten an unfinished document", "[Indent]") {
    const auto mode = HtmlMode();
    Buffer     buffer("test.html");
    buffer.InsertAtPoint("<html>\n  <body>\n    <div>\n      <ul>\n        x\n");

    IndentBuffer(buffer, mode);

    REQUIRE(buffer.Text() == "<html>\n  <body>\n    <div>\n      <ul>\n        x\n");
}

TEST_CASE("XmlMode indentColumn indents a nested element and aligns its closing tag", "[Indent]") {
    const auto mode = XmlMode();
    Buffer     buffer("test.xml");
    buffer.InsertAtPoint("<a>\n<b>x</b>\n</a>\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "<b>x</b>"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // xml's own built-in default is width 2 (IndentDefaults.h)
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "</a>"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("BashMode indentColumn indents an if-body and aligns fi with its own if", "[Indent]") {
    const auto mode = BashMode();
    Buffer     buffer("test.sh");
    buffer.InsertAtPoint("if x; then\necho y\nfi\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "echo y"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // bash's own built-in default is width 2 (IndentDefaults.h)
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "fi"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("BashMode indentColumn indents a for-loop body via do_group and aligns done", "[Indent]") {
    const auto mode = BashMode();
    Buffer     buffer("test.sh");
    buffer.InsertAtPoint("for i in a b; do\necho $i\ndone\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "echo $i"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // bash's own built-in default is width 2 (IndentDefaults.h)
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "done"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("FishMode indentColumn indents an if-body and aligns end with its own if", "[Indent]") {
    const auto mode = FishMode();
    Buffer     buffer("test.fish");
    buffer.InsertAtPoint("if test 1\necho a\nend\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "echo a"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // fish's own built-in default is width 2 (IndentDefaults.h)
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "end"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

// indent-engine follow-up (ROADMAP.md's own watch-list entry): method/
// singleton_method/while/until bodies were never reindented at all --
// Editor/Grammar/GrammarImprint.cpp's static inference can't find any of
// the four (method/singleton_method's own closer sits inside a
// mid-sequence CHOICE the flattener doesn't descend into; while/until's
// own "do" wrapper node is already in the imprint table, but its literal
// "do" child is grammatically OPTIONAL and elided entirely in the far
// more common `while x\n...end` style, which ImprintBracket.cpp's
// DelimitersOf requires to be physically present). Fixed via a
// hand-authored Source/Languages/ruby/indents.janet -- the same
// "the imprint can't read it, write a query" precedent every other
// bundled language's own indents.janet already follows.
TEST_CASE("RubyMode indentColumn indents a method's own body regardless of parameter "
          "style, and aligns end with its own def",
          "[Indent]") {
    const auto mode = RubyMode();

    Buffer buffer("test.rb");
    buffer.InsertAtPoint("def foo(a, b)\n1\nend\n");
    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1);             // "1"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // ruby's own built-in default is width 2 (IndentDefaults.h)
    const auto [closeStart, closeEnd] = LineRange(buffer, 2);           // "end"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);

    Buffer bareBuffer("test2.rb");
    bareBuffer.InsertAtPoint("def foo a, b\n1\nend\n");
    const auto [bareStart, bareEnd] = LineRange(bareBuffer, 1); // "1"
    REQUIRE(mode.indentColumn(bareBuffer.Text(), bareStart, bareEnd) == 2);

    Buffer noParenBuffer("test3.rb");
    noParenBuffer.InsertAtPoint("def foo\n1\nend\n");
    const auto [noParenStart, noParenEnd] = LineRange(noParenBuffer, 1); // "1"
    REQUIRE(mode.indentColumn(noParenBuffer.Text(), noParenStart, noParenEnd) == 2);

    Buffer singletonBuffer("test4.rb");
    singletonBuffer.InsertAtPoint("def self.foo\n1\nend\n");
    const auto [singletonStart, singletonEnd] = LineRange(singletonBuffer, 1); // "1"
    REQUIRE(mode.indentColumn(singletonBuffer.Text(), singletonStart, singletonEnd) == 2);
}

// The far more common style -- no literal "do" written at all -- is the
// one the imprint's own pre-existing table entry can't reach at all
// (confirmed live via `tree-sitter parse`: the "do"-typed wrapper node
// has no anonymous "do" child in this form). The explicit-"do" form is
// checked too, confirming the new query-based capture doesn't regress
// what the imprint already handled.
TEST_CASE("RubyMode indentColumn indents a while/until body in BOTH the idiomatic "
          "keyword-less style and the explicit \"do\" style, aligning end either way",
          "[Indent]") {
    const auto mode = RubyMode();

    Buffer buffer("test.rb");
    buffer.InsertAtPoint("while x\n1\nend\n");
    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "1"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // "end"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);

    Buffer untilBuffer("test2.rb");
    untilBuffer.InsertAtPoint("until x\n1\nend\n");
    const auto [untilStart, untilEnd] = LineRange(untilBuffer, 1); // "1"
    REQUIRE(mode.indentColumn(untilBuffer.Text(), untilStart, untilEnd) == 2);

    Buffer doBuffer("test3.rb");
    doBuffer.InsertAtPoint("while x do\n1\nend\n");
    const auto [doStart, doEnd] = LineRange(doBuffer, 1); // "1"
    REQUIRE(mode.indentColumn(doBuffer.Text(), doStart, doEnd) == 2);
}

// The real corruption hazard this fix had to avoid: Ruby's own "endless
// method" (`def f = 1`) has no "end" token at all, so an unconditional
// "(method) @indent" would open a container nothing ever dedents,
// over-indenting every subsequent line in the file. Both @indent and
// @dedent in indents.janet require "end" as a structural (unnamed)
// child of the SAME pattern, so neither ever fires without the other.
TEST_CASE("RubyMode indentColumn does not over-indent past an endless method (no \"end\" "
          "at all, correctly excluded from the indent container)",
          "[Indent]") {
    const auto mode = RubyMode();
    Buffer     buffer("test.rb");
    buffer.InsertAtPoint("def endless = 1\ndef after\n1\nend\n");

    const auto [afterBodyStart, afterBodyEnd] = LineRange(buffer, 2); // "1"
    REQUIRE(mode.indentColumn(buffer.Text(), afterBodyStart, afterBodyEnd) == 2);
    const auto [afterEndStart, afterEndEnd] = LineRange(buffer, 3); // "end"
    REQUIRE(mode.indentColumn(buffer.Text(), afterEndStart, afterEndEnd) == 0);
}

// End to end: a real IndentBuffer pass over nested class/method/if/while
// bodies, applied to a full Buffer and checked as a whole -- not just the
// computed column list -- per this project's own standing discipline.
TEST_CASE("End to end: IndentBuffer correctly nests class/method/if/while bodies in a "
          "real ruby-mode buffer",
          "[Indent]") {
    const auto mode = RubyMode();
    Buffer     buffer("test.rb");
    buffer.InsertAtPoint("class Greeter\n"
                         "def greet(name)\n"
                         "if name\n"
                         "puts name\n"
                         "end\n"
                         "end\n"
                         "end\n"
                         "while x\n"
                         "1\n"
                         "end\n");

    IndentBuffer(buffer, mode);

    REQUIRE(buffer.Text() == "class Greeter\n"
                             "  def greet(name)\n"
                             "    if name\n"
                             "      puts name\n"
                             "    end\n"
                             "  end\n"
                             "end\n"
                             "while x\n"
                             "  1\n"
                             "end\n");
}

TEST_CASE("JanetMode indentColumn aligns an ordinary call's continuation right after the opener, and its own "
          "closing paren with its opening line",
          "[Indent]") {
    // real-per-form-lisp-indent follow-up: "a" isn't a recognized special
    // form, so this form is plain @aligned -- and since "a" (the operator
    // itself) is the only thing following "(" on its own line, that's what
    // the continuation aligns to (column 1, right after "("), not a flat
    // bracket-depth level.
    const auto mode = JanetMode();
    Buffer     buffer("test.janet");
    buffer.InsertAtPoint("(a\nb\n)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "b"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 1);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // ")"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("ClojureMode indentColumn aligns an ordinary call's continuation right after the opener, and its own "
          "closing paren with its opening line",
          "[Indent]") {
    const auto mode = ClojureMode();
    Buffer     buffer("test.clj");
    buffer.InsertAtPoint("(a\nb\n)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "b"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 1);
    const auto [closeStart, closeEnd] = LineRange(buffer, 2); // ")"
    REQUIRE(mode.indentColumn(buffer.Text(), closeStart, closeEnd) == 0);
}

TEST_CASE("JankMode indentColumn shares Clojure's own indentation", "[Indent]") {
    const auto mode = JankMode();
    REQUIRE(mode.indentColumn);
    Buffer buffer("test.jank");
    buffer.InsertAtPoint("(a\nb)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "b)"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 1);
}

TEST_CASE("JanetMode indentColumn indents a let form's body a fixed 2 columns past its own column", "[Indent]") {
    const auto mode = JanetMode();
    Buffer     buffer("test.janet");
    buffer.InsertAtPoint("(let [x 1]\n  body)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "  body)"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2);
}

TEST_CASE("JanetMode indentColumn indents a defn form's body the same fixed 2 columns", "[Indent]") {
    const auto mode = JanetMode();
    Buffer     buffer("test.janet");
    buffer.InsertAtPoint("(defn foo [x]\n  body)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "  body)"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2);
}

TEST_CASE("JanetMode indentColumn falls back to plain bracket-depth when an ordinary call's opener has nothing "
          "following it on its own line",
          "[Indent]") {
    const auto mode = JanetMode();
    Buffer     buffer("test.janet");
    buffer.InsertAtPoint("(\n  foo a\n  b)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "  b)"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // janet's own built-in default is width 2 (IndentDefaults.h)
}

TEST_CASE("ClojureMode indentColumn indents a let form's body a fixed 2 columns past its own column", "[Indent]") {
    const auto mode = ClojureMode();
    Buffer     buffer("test.clj");
    buffer.InsertAtPoint("(let [x 1]\n  body)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "  body)"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2);
}

TEST_CASE("ClojureMode indentColumn indents a defn form's body the same fixed 2 columns", "[Indent]") {
    const auto mode = ClojureMode();
    Buffer     buffer("test.clj");
    buffer.InsertAtPoint("(defn foo [x]\n  body)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "  body)"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2);
}

TEST_CASE("ClojureMode indentColumn falls back to plain bracket-depth when an ordinary call's opener has nothing "
          "following it on its own line",
          "[Indent]") {
    const auto mode = ClojureMode();
    Buffer     buffer("test.clj");
    buffer.InsertAtPoint("(\n  foo a\n  b)\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "  b)"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // clojure's own built-in default is width 2 (IndentDefaults.h)
}

TEST_CASE("YamlMode indentColumn indents one level per genuinely nested mapping, not the document root",
          "[Indent]") {
    const auto mode = YamlMode();
    Buffer     buffer("test.yaml");
    buffer.InsertAtPoint("a:\n  b:\n    c: 1\n");

    const auto [level1Start, level1End] = LineRange(buffer, 1); // "  b:"
    REQUIRE(mode.indentColumn(buffer.Text(), level1Start, level1End) == 2); // yaml's own built-in default is width 2 (IndentDefaults.h)
    const auto [level2Start, level2End] = LineRange(buffer, 2); // "    c: 1"
    REQUIRE(mode.indentColumn(buffer.Text(), level2Start, level2End) == 4);
}

TEST_CASE("YamlMode indentColumn indents a nested sequence item", "[Indent]") {
    const auto mode = YamlMode();
    Buffer     buffer("test.yaml");
    buffer.InsertAtPoint("a:\n  - x\n  - y\n");

    const auto [itemStart, itemEnd] = LineRange(buffer, 1); // "  - x"
    REQUIRE(mode.indentColumn(buffer.Text(), itemStart, itemEnd) == 2); // yaml's own built-in default is width 2 (IndentDefaults.h)
}

TEST_CASE("YamlMode indentColumn takes a following mapping's level for a comment as the "
          "first line of that mapping, not the parent key's own level",
          "[Indent]") {
    // Same shape as the Python "comment as first line of a body" fix
    // (Editor/Indent.cpp) -- yaml's "block_mapping" is also an
    // opener-less indentation body (DelimiterKind::Indent,
    // openerIsFirst=false), so a comment immediately under "a:" attaches
    // as a sibling one level short of the nested mapping it precedes,
    // exactly like Python's "block" under "def f():". Proves the fix is
    // grammar-generic (Node::IsExtra-driven), not Python-specific.
    const auto mode = YamlMode();
    Buffer     buffer("test.yaml");
    buffer.InsertAtPoint("a:\n  # a leading comment\n  b: 1\n");

    const auto [commentStart, commentEnd] = LineRange(buffer, 1);             // "  # a leading comment"
    REQUIRE(mode.indentColumn(buffer.Text(), commentStart, commentEnd) == 2); // matches "b: 1"'s own level

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2); // "  b: 1"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2);
}

TEST_CASE("TomlMode indentColumn indents inside a multi-line array and aligns its closing bracket", "[Indent]") {
    const auto mode = TomlMode();
    Buffer     buffer("test.toml");
    buffer.InsertAtPoint("a = [\n1,\n2\n]\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "1,"
    REQUIRE(mode.indentColumn(buffer.Text(), bodyStart, bodyEnd) == 2); // toml's own built-in default is width 2 (IndentDefaults.h)
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

    const auto [contStart, contEnd] = LineRange(buffer, 1);             // "  more text" -- hanging continuation
    REQUIRE(mode.indentColumn(buffer.Text(), contStart, contEnd) == 2); // "- " is 2 columns wide
}

TEST_CASE("OrgMode indentColumn breaks out of a list on a second consecutive blank Enter", "[Indent]") {
    const auto mode = OrgMode();
    Buffer     buffer("test.org");
    buffer.InsertAtPoint("- item one\n  \n");

    const std::size_t newLinePos = buffer.Content().ByteLength();
    const auto        column     = mode.indentColumn(buffer.Text(), newLinePos, newLinePos);
    REQUIRE(column.has_value());
    REQUIRE(*column == 0);
}

TEST_CASE("OrgMode indentColumn still hangs a blank continuation on the FIRST Enter (single blank line)",
          "[Indent]") {
    const auto mode = OrgMode();
    Buffer     buffer("test.org");
    buffer.InsertAtPoint("- item one\n");

    const std::size_t newLinePos = buffer.Content().ByteLength();
    const auto        column     = mode.indentColumn(buffer.Text(), newLinePos, newLinePos);
    REQUIRE(column.has_value());
    REQUIRE(*column == 2);
}

TEST_CASE("OrgMode indentColumn never hangs a headline directly following a list item", "[Indent]") {
    // ROADMAP.md watch-list entry: tree-sitter-org's own `listitem` node
    // byte range reaches THROUGH a directly-following headline line with
    // no blank line between them (confirmed via a real parse dump), so
    // the ancestor walk used to resolve inside that listitem and hang the
    // headline's stars to its own bullet width -- a reindent would shift
    // the star and corrupt the outline. Real Org syntax requires a
    // headline's stars to start at column 0, so this is unambiguous.
    const auto mode = OrgMode();
    Buffer     buffer("test.org");
    buffer.InsertAtPoint("- [ ] unchecked box\n* Second tree\n");

    const auto [headlineStart, headlineEnd] = LineRange(buffer, 1); // "* Second tree"
    REQUIRE(mode.indentColumn(buffer.Text(), headlineStart, headlineEnd) == 0);
}

TEST_CASE("CppMode indentColumn aligns a wrapped call's continuation argument to the first argument's column",
          "[Indent]") {
    const auto mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("int r = foo(a,\n            b);\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1); // "            b);"
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    REQUIRE(*contColumn == 12); // aligns under "a", the byte right after "(" -- same as CMode
}

// lambda-body-alignment follow-up. A block-bodied callable passed as a call
// argument used to inherit the argument list's own @aligned column, so every
// line of the lambda's body landed at "(" + one level per nesting depth
// instead of one level past the owning statement -- a live-reported bug
// ("really deep tabulation"), reproduced here byte-for-byte from the real
// main.cpp construct that surfaced it. @align.barrier on compound_statement
// (cpp-indents.scm) is what stops the argument_list's alignment reaching in.
TEST_CASE("CppMode indentColumn indents a lambda argument's body from the statement, not the call's paren column",
          "[Indent]") {
    const auto mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("void f() {\n"
                         "    std::jthread stdinToSocket([fd] {\n"
                         "        char buffer[4096];\n"
                         "        while (true) {\n"
                         "            if (n <= 0) {\n"
                         "                break;\n"
                         "            }\n"
                         "        }\n"
                         "    });\n"
                         "}\n");

    const auto columnOfLine = [&](std::size_t line) {
        const auto [start, end] = LineRange(buffer, line);
        const auto column       = mode.indentColumn(buffer.Text(), start, end);
        REQUIRE(column.has_value());
        return *column;
    };

    REQUIRE(columnOfLine(2) == 8);  // "char buffer[4096];" -- f's body + the lambda's
    REQUIRE(columnOfLine(3) == 8);  // "while (true) {"
    REQUIRE(columnOfLine(4) == 12); // "if (n <= 0) {"
    REQUIRE(columnOfLine(5) == 16); // "break;"
    REQUIRE(columnOfLine(6) == 12); // the if's own "}"
    REQUIRE(columnOfLine(7) == 8);  // the while's own "}"
    // The closing "});" resolves through the dedent branch, which computes
    // "as if for the barrier's own opening line" -- seeding the walk AT the
    // barrier itself. It must still land on the owning statement's level,
    // which is why the barrier is deliberately not gated on opensAtPosition.
    REQUIRE(columnOfLine(8) == 4);
}

TEST_CASE("JavaScriptMode indentColumn indents a callback body from the statement, not the call's paren column",
          "[Indent]") {
    const auto mode = JavaScriptMode();
    Buffer     buffer("test.js");
    buffer.InsertAtPoint("setTimeout(() => {\n"
                         "    doThing();\n"
                         "}, 100);\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1);
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 2); // one level from the statement, javascript's own built-in default is width 2 (IndentDefaults.h)
}

TEST_CASE("GoMode indentColumn indents a func literal's body from the statement, not the call's paren column",
          "[Indent]") {
    const auto mode = GoMode();
    Buffer     buffer("test.go");
    buffer.InsertAtPoint("func main() {\n"
                         "\tgo doStuff(func() {\n"
                         "\t\tx()\n"
                         "\t})\n"
                         "}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2);
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 2 * EffectiveIndentStyle("go-mode").width);
}

TEST_CASE("RustMode indentColumn indents a closure body from the statement, not the call's paren column",
          "[Indent]") {
    const auto mode = RustMode();
    Buffer     buffer("test.rs");
    buffer.InsertAtPoint("fn main() {\n"
                         "    thread::spawn(move || {\n"
                         "        work();\n"
                         "    });\n"
                         "}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 2);
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 8);
}

TEST_CASE("JavaMode indentColumn indents an anonymous class body from the statement, not the call's paren column",
          "[Indent]") {
    const auto mode = JavaMode();
    Buffer     buffer("Test.java");
    buffer.InsertAtPoint("class C {\n"
                         "    void m() {\n"
                         "        submit(new Runnable() {\n"
                         "            public void run() {\n"
                         "                work();\n"
                         "            }\n"
                         "        });\n"
                         "    }\n"
                         "}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 3);
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 12);
}

// @align.barrier is opt-in per query precisely so a language whose nested
// @indent containers SHOULD inherit an enclosing call's alignment keeps
// doing so -- janet/clojure never use the capture, and a "[...]" inside a
// "(foo ...)" call still resolves through the call's own @aligned column.
// This pins the pass-through, not the (separately imperfect) column itself.
TEST_CASE("JanetMode indentColumn still lets a nested bracket inherit the enclosing call's alignment", "[Indent]") {
    const auto mode = JanetMode();
    Buffer     buffer("test.janet");
    buffer.InsertAtPoint("(foo bar [a\n          b])\n");

    const auto [contStart, contEnd] = LineRange(buffer, 1);
    const auto contColumn           = mode.indentColumn(buffer.Text(), contStart, contEnd);
    REQUIRE(contColumn.has_value());
    // The enclosing call's own alignment column (right after "(", since "foo"
    // is the only thing following the opener on its own line) plus one level
    // -- the vector's own nesting -- counted strictly inside it: 1 + 1 *
    // janet's own built-in width (2, IndentDefaults.h). Deliberately not "the
    // byte column under 'bar'" (a coincidental reading that only held while
    // the old flat default was 4, since 1 + 1*4 == 5 == that column too).
    REQUIRE(*contColumn == 3);
}

TEST_CASE("CppMode indentColumn does not indent a top-level namespace's own body", "[Indent]") {
    // NamespaceIndentation: Inner (.clang-format) -- only a namespace nested
    // inside another namespace indents its body; an ordinary top-level one
    // (named or anonymous) does not. A real live-reported bug: a newline
    // typed after an ordinary statement directly inside a top-level
    // `namespace { ... }` landed one level too deep (8 columns instead of
    // 4) because declaration_list (the namespace's own body) was counted as
    // a real indent level regardless of nesting.
    const auto mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("namespace {\nvoid f() {\n    g();\n}\n}\n");

    const auto [bodyStart, bodyEnd] = LineRange(buffer, 1); // "void f() {"
    const auto bodyColumn           = mode.indentColumn(buffer.Text(), bodyStart, bodyEnd);
    REQUIRE(bodyColumn.has_value());
    REQUIRE(*bodyColumn == 0); // top-level namespace body: no extra level

    const auto [stmtStart, stmtEnd] = LineRange(buffer, 2); // "    g();"
    const auto stmtColumn           = mode.indentColumn(buffer.Text(), stmtStart, stmtEnd);
    REQUIRE(stmtColumn.has_value());
    REQUIRE(*stmtColumn == 4); // one level for f()'s own compound_statement, not two
}

TEST_CASE("CppMode indentColumn indents a namespace genuinely nested inside another namespace", "[Indent]") {
    const auto mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("namespace outer {\nnamespace inner {\nvoid f() {\n}\n}\n}\n");

    const auto [nestedStart, nestedEnd] = LineRange(buffer, 2); // "void f() {"
    const auto nestedColumn             = mode.indentColumn(buffer.Text(), nestedStart, nestedEnd);
    REQUIRE(nestedColumn.has_value());
    REQUIRE(*nestedColumn == 4); // one level, for being inside the genuinely-nested "inner"
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

TEST_CASE("IndentBuffer reindents correctly on a huge buffer via the windowed path", "[Indent][HugeFile]") {
    const HugeStructuralWindowBytesGuard guard;
    // A tiny margin -- smaller than most of this fixture's own lines -- so
    // the windowed path genuinely engages (windowStart/windowEnd land
    // strictly inside the document, not "the whole file" by coincidence),
    // not just exercises the huge=true/false branch with an effectively
    // unbounded window.
    ned::editor::SetHugeStructuralWindowBytes(8);

    const std::filesystem::path path =
        WriteTempFile("ned_indent_huge_windowed.c", "int f(void) {\nif (1) {\n           return 0;\n}\n}\n");
    Buffer buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Content().IsHuge());

    const auto mode = CMode();
    REQUIRE(IndentBuffer(buffer, mode) > 0);
    // Identical to the non-huge "reindent a deliberately misindented C
    // file" test above -- windowing must not change the actual result, only
    // how much text mode.indentColumn sees at once.
    REQUIRE(buffer.Text() == "int f(void) {\n    if (1) {\n        return 0;\n    }\n}\n");
}

TEST_CASE("RigidShiftRegion indents every line in range by one width, mode-agnostic", "[Indent]") {
    // No Mode/indentColumn involved at all -- proves this is genuinely
    // mode-agnostic, unlike IndentRegion's own tree-sitter recompute.
    Buffer buffer("test.txt");
    buffer.InsertAtPoint("a\nb\nc\n");
    const IndentStyle style{.useTabs = false, .width = 4};

    const std::size_t changed = RigidShiftRegion(buffer, style, 0, 3, 1);
    REQUIRE(changed == 3);
    REQUIRE(buffer.Text() == "    a\n    b\n    c\n");

    // A second indent stacks additively, not replacing.
    RigidShiftRegion(buffer, style, 0, 3, 1);
    REQUIRE(buffer.Text() == "        a\n        b\n        c\n");
}

TEST_CASE("RigidShiftRegion dedents every line in range by one width, floored at zero", "[Indent]") {
    Buffer buffer("test.txt");
    buffer.InsertAtPoint("    a\n  b\nc\n"); // 4, 2, 0 columns of existing indent
    const IndentStyle style{.useTabs = false, .width = 4};

    RigidShiftRegion(buffer, style, 0, 3, -1);
    // "    a" (4) drops to 0; "  b" (2) and "c" (0) were already below one
    // width and both floor at 0 rather than going negative.
    REQUIRE(buffer.Text() == "a\nb\nc\n");
}

TEST_CASE("RigidShiftRegion measures existing tabs via the configured tab width before shifting", "[Indent]") {
    const int previousTabWidth = ned::editor::TabWidth();
    ned::editor::SetTabWidth(8);
    struct RestoreGuard {
        int width;
        ~RestoreGuard() {
            ned::editor::SetTabWidth(width);
        }
    } restoreGuard{previousTabWidth};

    Buffer buffer("test.txt");
    buffer.InsertAtPoint("\ta\n"); // one literal tab -- visual column 8
    const IndentStyle style{.useTabs = false, .width = 4};

    RigidShiftRegion(buffer, style, 0, 1, 1);
    // 8 (the tab's own visual width) + 4 (one style.width) = 12 spaces --
    // and the literal tab itself is replaced (IndentString never re-uses
    // useTabs=false's own leftover tab byte).
    REQUIRE(buffer.Text() == std::string(12, ' ') + "a\n");
}

TEST_CASE("MarkdownMode indentColumn re-affirms a blank continuation's own hang after it's "
          "already been auto-indented once, instead of collapsing it to column 0",
          "[Indent]") {
    // tab-after-newline-blank-line-collapse follow-up: "newline" queries
    // indentColumn with the zero-width (lineStart == lineEnd) "not-yet-
    // typed" convention and writes the real spaces itself; a SECOND query
    // against that SAME now-real blank line (lineStart != lineEnd, 4 real
    // space bytes already there -- exactly what a subsequent TAB press
    // asks) used to miss the smart-blank-line rescue entirely (it checked
    // lineStart == lineEnd specifically) and silently resolve to column 0,
    // undoing the indent "newline" had just computed one keystroke
    // earlier. Confirmed live in a real session, not assumed.
    const auto mode = MarkdownMode();
    Buffer     buffer("test.md");
    buffer.InsertAtPoint("- item one\n    "); // "newline"'s own auto-indent (one step), already applied

    const std::size_t lineStart     = buffer.Content().ByteOffsetToLine(buffer.Point());
    const std::size_t lineStartByte = buffer.Content().LineToByteOffset(lineStart);
    const std::size_t lineEnd       = buffer.Content().ByteLength();
    REQUIRE(lineEnd - lineStartByte == 4); // sanity: the 4 real space bytes are there

    const auto column = ned::editor::IndentColumnForLine(mode, buffer.Text(), lineStartByte, lineEnd);
    REQUIRE(column.has_value());
    REQUIRE(*column == 4); // not 0
}

TEST_CASE("MarkdownMode indentColumn re-affirms a checkbox item's own hang after auto-indent, "
          "the same one indent step a plain bullet gets",
          "[Indent]") {
    // checkbox-hang-matches-tab-depth follow-up: a checkbox item ("- [ ] ")
    // and a plain bullet ("- ") now hang identically -- one configured
    // step, not either marker's own literal width. Confirms the collapse-
    // to-0 fix above (same shape: a re-query against an already-auto-
    // indented blank line) holds for a checkbox item too, now that
    // marker-width is no longer part of the computation at all.
    const auto mode = MarkdownMode();
    Buffer     buffer("test.md");
    buffer.InsertAtPoint("- [ ] item one\n    ");

    const std::size_t lineStart     = buffer.Content().ByteOffsetToLine(buffer.Point());
    const std::size_t lineStartByte = buffer.Content().LineToByteOffset(lineStart);
    const std::size_t lineEnd       = buffer.Content().ByteLength();

    const auto column = ned::editor::IndentColumnForLine(mode, buffer.Text(), lineStartByte, lineEnd);
    REQUIRE(column.has_value());
    REQUIRE(*column == 4);
}
