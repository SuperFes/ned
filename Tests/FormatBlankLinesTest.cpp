#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatBlankLines.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::ComputeBlankLineEdits;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::Mode;
using ned::editor::PythonMode;
using ned::editor::SetBlankMaxBefore;
using ned::editor::SetBlankMinBefore;
using ned::text::Buffer;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetBlankMinBefore("def.toplevel", std::nullopt);
        SetBlankMaxBefore("def.toplevel", std::nullopt);
        SetBlankMinBefore("def.method", std::nullopt);
        SetBlankMaxBefore("def.method", std::nullopt);
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

TEST_CASE("ComputeBlankLineEdits does nothing when no rule is configured", "[FormatBlankLines]") {
    const std::string source = "def a():\n    pass\ndef b():\n    pass\n";
    const FormatCapture defB{"def.toplevel", source.find("def b"), source.size() - 1, false, false};
    REQUIRE(ComputeBlankLineEdits(source, "python", {defB}).empty());
}

TEST_CASE("minBefore inserts missing blank lines", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.toplevel", 2);

    const std::string source = "def a():\n    pass\ndef b():\n    pass\n";
    const std::size_t startB = source.find("def b");
    const FormatCapture defB{"def.toplevel", startB, source.size() - 1, false, false};

    Buffer buffer("test.py");
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "python", {defB}));

    REQUIRE(buffer.Text() == "def a():\n    pass\n\n\ndef b():\n    pass\n");
}

TEST_CASE("minBefore is a no-op when already satisfied", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.toplevel", 2);

    const std::string   source = "def a():\n    pass\n\n\ndef b():\n    pass\n";
    const std::size_t   startB = source.find("def b");
    const FormatCapture defB{"def.toplevel", startB, source.size() - 1, false, false};

    REQUIRE(ComputeBlankLineEdits(source, "python", {defB}).empty());
}

TEST_CASE("minBefore is skipped when the capture is first in its container", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.method", 1);

    const std::string   source = "class C:\n    def a(self):\n        pass\n";
    const std::size_t   startA = source.find("def a");
    const FormatCapture defA{"def.method", startA, source.size() - 1, false, /*isFirst=*/true};

    REQUIRE(ComputeBlankLineEdits(source, "python", {defA}).empty());
}

TEST_CASE("maxBefore trims excess blank lines", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMaxBefore("def.toplevel", 1);

    const std::string   source = "def a():\n    pass\n\n\n\ndef b():\n    pass\n";
    const std::size_t   startB = source.find("def b");
    const FormatCapture defB{"def.toplevel", startB, source.size() - 1, false, false};

    Buffer buffer("test.py");
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "python", {defB}));

    REQUIRE(buffer.Text() == "def a():\n    pass\n\ndef b():\n    pass\n");
}

TEST_CASE("maxBefore trims excess blank lines even when the capture is first in its container",
          "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMaxBefore("def.method", 0);

    const std::string   source = "class C:\n\n\n    def a(self):\n        pass\n";
    const std::size_t   startA = source.find("def a");
    const FormatCapture defA{"def.method", startA, source.size() - 1, false, /*isFirst=*/true};

    Buffer buffer("test.py");
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "python", {defA}));

    REQUIRE(buffer.Text() == "class C:\n    def a(self):\n        pass\n");
}

TEST_CASE("minBefore and maxBefore compose as a clamp", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.toplevel", 1);
    SetBlankMaxBefore("def.toplevel", 2);

    const std::string   zeroBlank = "def a():\n    pass\ndef b():\n    pass\n";
    const FormatCapture zeroCap{"def.toplevel", zeroBlank.find("def b"), zeroBlank.size() - 1, false, false};
    Buffer bufferZero("test.py");
    bufferZero.InsertAtPoint(zeroBlank);
    ApplyFormatTextEdits(bufferZero, ComputeBlankLineEdits(bufferZero.Text(), "python", {zeroCap}));
    REQUIRE(bufferZero.Text() == "def a():\n    pass\n\ndef b():\n    pass\n");

    const std::string   fourBlank = "def a():\n    pass\n\n\n\n\ndef b():\n    pass\n";
    const FormatCapture fourCap{"def.toplevel", fourBlank.find("def b"), fourBlank.size() - 1, false, false};
    Buffer bufferFour("test.py");
    bufferFour.InsertAtPoint(fourBlank);
    ApplyFormatTextEdits(bufferFour, ComputeBlankLineEdits(bufferFour.Text(), "python", {fourCap}));
    REQUIRE(bufferFour.Text() == "def a():\n    pass\n\n\ndef b():\n    pass\n");
}

// python-mode: the language this rule kind was built for.
TEST_CASE("python-mode's format.janet names def.toplevel/def.method with correct .first markers",
          "[FormatBlankLines]") {
    const Mode mode = PythonMode();

    const std::string source = "class C:\n"
                                "    def a(self):\n"
                                "        pass\n"
                                "    def b(self):\n"
                                "        pass\n";
    const auto captures = mode.formatCaptures(source);
    const auto methods   = CapturesNamed(captures, "def.method");
    REQUIRE(methods.size() == 2);
    REQUIRE(methods[0].isFirst);
    REQUIRE_FALSE(methods[1].isFirst);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 1);
    REQUIRE(toplevel[0].isFirst); // the class itself is the only/first top-level construct
}

TEST_CASE("End to end: PEP8-style blank lines applied to a real python-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.toplevel", 2);
    SetBlankMinBefore("def.method", 1);

    const Mode mode = PythonMode();
    Buffer     buffer("test.py");
    // "def a" has genuinely nothing above it (isFirst) -- no forced blank
    // line at the very top of the file. "class C" has a real preceding
    // sibling (def a's own body), so it's not first -- 2 blank lines
    // forced despite being module-level too, same as JetBrains' own
    // "around function/class" rule applying uniformly at that level.
    buffer.InsertAtPoint("def a():\n"
                         "    pass\n"
                         "class C:\n"
                         "    def m1(self):\n"
                         "        pass\n"
                         "    def m2(self):\n"
                         "        pass\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "python", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "def a():\n" // def.toplevel and isFirst -- minBefore skipped
                             "    pass\n"
                             "\n\n"
                             "class C:\n" // def.toplevel, not first -- 2 blank lines forced
                             "    def m1(self):\n" // def.method but isFirst -- minBefore skipped
                             "        pass\n"
                             "\n"
                             "    def m2(self):\n" // def.method, not first -- 1 blank line forced
                             "        pass\n");

    // Idempotent: re-running against the now-formatted buffer finds nothing more to do.
    REQUIRE(ComputeBlankLineEdits(buffer.Text(), "python", mode.formatCaptures(buffer.Text())).empty());
}
