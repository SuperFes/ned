#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatArrange.h"
#include "Editor/FormatEdit.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::ArrangeRuleFor;
using ned::editor::ComputeArrangeEdits;
using ned::editor::CppMode;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::JavaScriptMode;
using ned::editor::Mode;
using ned::editor::PythonMode;
using ned::editor::SetArrangeCaseInsensitive;
using ned::editor::SetArrangeEnabled;
using ned::text::Buffer;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetArrangeEnabled("arrange.import", std::nullopt);
        SetArrangeCaseInsensitive("arrange.import", std::nullopt);
        SetArrangeEnabled("cpp/arrange.import", std::nullopt);
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

TEST_CASE("cpp-mode's format.janet names arrange.import on a whole #include directive",
          "[FormatArrange]") {
    const Mode mode     = CppMode();
    const auto captures = CapturesNamed(mode.formatCaptures("#include <a.h>\n"), "arrange.import");
    REQUIRE(captures.size() == 1);
}

TEST_CASE("javascript-mode's format.janet names arrange.import on a whole import statement",
          "[FormatArrange]") {
    const Mode mode     = JavaScriptMode();
    const auto captures = CapturesNamed(mode.formatCaptures("import a from 'a';\n"), "arrange.import");
    REQUIRE(captures.size() == 1);
}

TEST_CASE("ComputeArrangeEdits does nothing when unconfigured", "[FormatArrange]") {
    const Mode        mode   = CppMode();
    const std::string source = "#include <b.h>\n#include <a.h>\n";
    REQUIRE(ComputeArrangeEdits(source, "cpp", mode.formatCaptures(source)).empty());
}

TEST_CASE("cpp's own preproc_include capture includes its own trailing newline; two adjacent "
          "directives are still two independent captures",
          "[FormatArrange]") {
    const Mode        mode     = CppMode();
    const std::string source   = "#include <b.h>\n#include <a.h>\n";
    const auto        captures = CapturesNamed(mode.formatCaptures(source), "arrange.import");
    REQUIRE(captures.size() == 2);
    REQUIRE(captures[0].startByte == 0);
    REQUIRE(captures[0].endByte == 15); // includes the '\n' -- see FormatArrange.cpp's own WithoutOneTrailingNewline
    REQUIRE(captures[1].startByte == 15);
    REQUIRE(captures[1].endByte == 30);
}

TEST_CASE("End to end: an adjacent run of #include directives sorts by the captured text",
          "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);

    const Mode mode = CppMode();
    Buffer     buffer("t.cpp");
    buffer.InsertAtPoint("#include <b.h>\n#include <a.h>\n");
    ApplyFormatTextEdits(buffer, ComputeArrangeEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "#include <a.h>\n#include <b.h>\n");
}

TEST_CASE("End to end: an adjacent run of JavaScript import statements sorts by the captured text",
          "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);

    const Mode mode = JavaScriptMode();
    Buffer     buffer("t.js");
    buffer.InsertAtPoint("import z from 'z';\nimport a from 'a';\n");
    ApplyFormatTextEdits(buffer,
                         ComputeArrangeEdits(buffer.Text(), "javascript", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "import a from 'a';\nimport z from 'z';\n");
}

TEST_CASE("End to end: idempotent -- an already-sorted run changes nothing", "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);

    const Mode        mode   = CppMode();
    const std::string source = "#include <a.h>\n#include <b.h>\n";
    Buffer            buffer("t.cpp");
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, ComputeArrangeEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == source);
}

TEST_CASE("A blank line between two #include directives breaks the run -- neither gets reordered",
          "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);

    const Mode        mode   = CppMode();
    const std::string source = "#include <b.h>\n\n#include <a.h>\n";
    REQUIRE(ComputeArrangeEdits(source, "cpp", mode.formatCaptures(source)).empty());
}

TEST_CASE("End to end: :case-insensitive folds ASCII case before comparing", "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);

    const Mode mode = JavaScriptMode();

    // Ordinal (case-sensitive, the default): 'B' (0x42) sorts before 'a'
    // (0x61), so this run is NOT already sorted and gets reordered.
    {
        Buffer buffer("t.js");
        buffer.InsertAtPoint("import a from 'a';\nimport B from 'b';\n");
        ApplyFormatTextEdits(buffer, ComputeArrangeEdits(buffer.Text(), "javascript", mode.formatCaptures(buffer.Text())));
        REQUIRE(buffer.Text() == "import B from 'b';\nimport a from 'a';\n");
    }

    // Case-insensitive: 'a' < 'b' once folded, so the same original order is
    // already sorted and nothing changes.
    SetArrangeCaseInsensitive("arrange.import", true);
    {
        Buffer buffer("t.js");
        buffer.InsertAtPoint("import a from 'a';\nimport B from 'b';\n");
        ApplyFormatTextEdits(buffer, ComputeArrangeEdits(buffer.Text(), "javascript", mode.formatCaptures(buffer.Text())));
        REQUIRE(buffer.Text() == "import a from 'a';\nimport B from 'b';\n");
    }
}

TEST_CASE("A multi-line import is declined -- never reordered, and doesn't block its neighbors "
          "from being treated as their own (singleton) run",
          "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);

    const Mode        mode   = JavaScriptMode();
    const std::string source = "import {\n  a,\n  b\n} from 'x';\nimport c from 'y';\n";
    REQUIRE(ComputeArrangeEdits(source, "javascript", mode.formatCaptures(source)).empty());
}

// arrange-kind widening: a second language, Python -- see python/format.janet's
// own header comment for arrange.import.
TEST_CASE("python-mode's format.janet names arrange.import on both \"import x\" and \"from x "
          "import y\", one capture each, with no trailing newline in the span",
          "[FormatArrange]") {
    const Mode mode = PythonMode();

    const auto plain = CapturesNamed(mode.formatCaptures("import a\nimport b\n"), "arrange.import");
    REQUIRE(plain.size() == 2);
    REQUIRE(plain[0].startByte == 0);
    REQUIRE(plain[0].endByte == 8); // "import a" -- no trailing '\n'
    REQUIRE(plain[1].startByte == 9);
    REQUIRE(plain[1].endByte == 17);

    const auto from = CapturesNamed(mode.formatCaptures("from a import b\n"), "arrange.import");
    REQUIRE(from.size() == 1);
    REQUIRE(from[0].startByte == 0);
    REQUIRE(from[0].endByte == 15); // "from a import b" -- no trailing '\n'
}

TEST_CASE("End to end: an adjacent run of Python import statements sorts by the captured text",
          "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);

    const Mode mode = PythonMode();
    Buffer     buffer("t.py");
    buffer.InsertAtPoint("import z\nimport a\n");
    ApplyFormatTextEdits(buffer, ComputeArrangeEdits(buffer.Text(), "python", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "import a\nimport z\n");
}

TEST_CASE("A multi-line Python \"from x import (...)\" is declined -- never reordered",
          "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);

    const Mode        mode   = PythonMode();
    const std::string source = "from x import (\n    a,\n    b,\n)\nimport c\n";
    REQUIRE(ComputeArrangeEdits(source, "python", mode.formatCaptures(source)).empty());
}

TEST_CASE("ArrangeRuleFor(name, language) resolves the language-scoped key first, matching every "
          "other rule kind's own precedent",
          "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);
    SetArrangeEnabled("cpp/arrange.import", false);

    REQUIRE(ArrangeRuleFor("arrange.import", "cpp").enabled == false);
    REQUIRE(ArrangeRuleFor("arrange.import", "javascript").enabled == true); // falls through
}

TEST_CASE("CUDA, HLSL, Objective-C and GLSL share the C family's format captures", "[FormatRules]") {
    const std::string source = "int f(int x) {\n"
                               "    if (x) { return 1; }\n"
                               "    return 0;\n"
                               "}\n";
    for (const char* language : {"cuda-mode", "hlsl-mode", "objc-mode", "glsl-mode"}) {
        INFO(language);
        const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName(language);
        REQUIRE(mode.has_value());
        REQUIRE(static_cast<bool>(mode->formatCaptures));
        const auto captures = mode->formatCaptures(source);
        const auto function = CapturesNamed(captures, "brace.function");
        REQUIRE(function.size() == 1);
        CHECK(function[0].startByte == source.find('{'));
        CHECK(CapturesNamed(captures, "brace.control").size() == 1);
        CHECK(CapturesNamed(captures, "control.parens").size() == 1);
    }
}

namespace {

struct FormatSample {
    const char* mode;
    const char* source;
    std::size_t braceFunction;
    std::size_t braceControl;
    std::size_t braceClass;
    std::size_t controlParens;
    std::size_t controlKeyword;
    std::size_t simple;
};

// Each case's source has a function holding if/else and a loop, beside a
// class holding a one-statement method; every control body is one statement.
const FormatSample kFormatSamples[] = {
    {"d-mode",
     "class C { int m() { return 1; } }\n"
     "int f(int x) {\n  if (x) { return 1; } else { return 2; }\n  while (x) { x--; }\n}\n",
     2, 3, 1, 2, 1, 4},
    {"dart-mode",
     "class C { int m() { return 1; } }\n"
     "int f(int x) {\n  if (x > 0) { return 1; } else { return 2; }\n  while (x > 0) { x--; }\n}\n",
     2, 3, 1, 2, 1, 4},
    {"scala-mode",
     "class C { def m(): Int = { 1 } }\n"
     "def f(x: Int): Int = {\n  if (x > 0) { 1 } else { 2 }\n  while (x > 0) { g() }\n}\n",
     2, 3, 1, 2, 1, 4},
    {"groovy-mode",
     "class C { int m() { return 1 } }\n"
     "int f(int x) {\n  if (x > 0) { return 1 } else { return 2 }\n  while (x > 0) { x-- }\n}\n",
     2, 3, 1, 2, 1, 4},
    {"solidity-mode",
     "contract C {\n  function f(uint x) public returns (uint) {\n"
     "    if (x > 0) { return 1; } else { return 2; }\n    while (x > 0) { x--; }\n  }\n}\n",
     1, 3, 1, 2, 1, 3},
    {"vala-mode",
     "class C { int m() { return 1; } }\n"
     "int f(int x) {\n  if (x > 0) { return 1; } else { return 2; }\n  while (x > 0) { x--; }\n}\n",
     2, 3, 1, 2, 1, 4},
    {"v-mode", "fn f(x int) int {\n  if x > 0 { return 1 } else { return 2 }\n  for i in 0 .. 3 { g() }\n}\n", 1, 3, 0, 0,
     0, 3},
    {"odin-mode",
     "package p\nf :: proc(x: int) -> int {\n  if x > 0 { return 1 } else { return 2 }\n  for i in 0..<3 { g() }\n}\n", 1,
     3, 0, 0, 0, 3},
};

} // namespace

TEST_CASE("Each brace language's format captures name real brace and paren pairs", "[FormatRules]") {
    for (const FormatSample& sample : kFormatSamples) {
        const std::string source = sample.source;
        INFO(sample.mode << ":\n"
                         << source);
        const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName(sample.mode);
        REQUIRE(mode.has_value());
        REQUIRE(static_cast<bool>(mode->formatCaptures));
        const auto  captures = mode->formatCaptures(source);
        std::size_t simple   = 0;
        for (const FormatCapture& capture : captures) {
            INFO(capture.name << " [" << capture.startByte << "," << capture.endByte << ")");
            const std::string_view text(source.data() + capture.startByte, capture.endByte - capture.startByte);
            if (capture.name.starts_with("brace.")) {
                CHECK(text.substr(0, capture.openLength) == "{");
                CHECK(text.substr(text.size() - capture.closeLength) == "}");
                simple += capture.isSimple ? 1 : 0;
            }
            else if (capture.name == "control.parens") {
                CHECK(text.front() == '(');
                CHECK(text.back() == ')');
            }
        }
        CHECK(CapturesNamed(captures, "brace.function").size() == sample.braceFunction);
        CHECK(CapturesNamed(captures, "brace.control").size() == sample.braceControl);
        CHECK(CapturesNamed(captures, "brace.class").size() == sample.braceClass);
        CHECK(CapturesNamed(captures, "control.parens").size() == sample.controlParens);
        CHECK(CapturesNamed(captures, "control.keyword").size() == sample.controlKeyword);
        CHECK(simple == sample.simple);
    }
}
