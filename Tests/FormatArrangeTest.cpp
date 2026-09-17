#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatArrange.h"
#include "Editor/FormatEdit.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::ArrangeRuleFor;
using ned::editor::ComputeArrangeEdits;
using ned::editor::CppMode;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::JavaScriptMode;
using ned::editor::Mode;
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

TEST_CASE("ArrangeRuleFor(name, language) resolves the language-scoped key first, matching every "
          "other rule kind's own precedent",
          "[FormatArrange]") {
    const FormatRulesGuard guard;
    SetArrangeEnabled("arrange.import", true);
    SetArrangeEnabled("cpp/arrange.import", false);

    REQUIRE(ArrangeRuleFor("arrange.import", "cpp").enabled == false);
    REQUIRE(ArrangeRuleFor("arrange.import", "javascript").enabled == true); // falls through
}
