#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatBlankLines.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::ComputeBlankLineEdits;
using ned::editor::CMode;
using ned::editor::CppMode;
using ned::editor::CSharpMode;
using ned::editor::FormatCapture;
using ned::editor::FormatTextEdit;
using ned::editor::GoMode;
using ned::editor::JavaMode;
using ned::editor::JavaScriptMode;
using ned::editor::KotlinMode;
using ned::editor::Mode;
using ned::editor::PhpMode;
using ned::editor::PythonMode;
using ned::editor::RustMode;
using ned::editor::TypeScriptMode;
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

// blank-lines-kind rollout follow-up: rolled out to the six other
// languages. def.toplevel/def.method are the SAME capture names throughout
// -- one rule, several grammars, the same design already proven for
// control.parens/brace.*.
TEST_CASE("cpp-mode's format.janet names def.toplevel/def.method with correct .first markers",
          "[FormatBlankLines]") {
    const Mode mode = CppMode();

    const std::string source = "int f() {\n"
                               "    return 1;\n"
                               "}\n"
                               "struct C {\n"
                               "    int m() { return 1; }\n"
                               "    int n() { return 1; }\n"
                               "};\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 2);
    REQUIRE(toplevel[0].isFirst);       // f() -- nothing above it
    REQUIRE_FALSE(toplevel[1].isFirst); // struct C -- f() is a real preceding sibling

    const auto methods = CapturesNamed(captures, "def.method");
    REQUIRE(methods.size() == 2);
    REQUIRE(methods[0].isFirst);       // m() -- struct's own field list has no leading access_specifier
    REQUIRE_FALSE(methods[1].isFirst); // n()
}

TEST_CASE("End to end: blank lines applied to a real cpp-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.toplevel", 2);
    SetBlankMinBefore("def.method", 1);

    const Mode mode = CppMode();
    Buffer     buffer("test.cpp");
    buffer.InsertAtPoint("int f() {\n"
                         "    return 1;\n"
                         "}\n"
                         "struct C {\n"
                         "    int m() { return 1; }\n"
                         "    int n() { return 1; }\n"
                         "};\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "cpp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int f() {\n"
                             "    return 1;\n"
                             "}\n"
                             "\n\n"
                             "struct C {\n"
                             "    int m() { return 1; }\n"
                             "\n"
                             "    int n() { return 1; }\n"
                             "};\n");
}

TEST_CASE("javascript-mode's format.janet names def.toplevel/def.method with correct .first markers",
          "[FormatBlankLines]") {
    const Mode mode = JavaScriptMode();

    const std::string source = "function f() {}\n"
                               "class C {\n"
                               "    m() {}\n"
                               "    n() {}\n"
                               "}\n"
                               "export function g() {}\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 3);
    REQUIRE(toplevel[0].isFirst);
    REQUIRE_FALSE(toplevel[1].isFirst);
    REQUIRE_FALSE(toplevel[2].isFirst);

    const auto methods = CapturesNamed(captures, "def.method");
    REQUIRE(methods.size() == 2);
    REQUIRE(methods[0].isFirst);
    REQUIRE_FALSE(methods[1].isFirst);
}

TEST_CASE("End to end: blank lines applied to a real javascript-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.method", 1);

    const Mode mode = JavaScriptMode();
    Buffer     buffer("test.js");
    buffer.InsertAtPoint("class C {\n"
                         "    m() {}\n"
                         "    n() {}\n"
                         "}\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "javascript", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "class C {\n"
                             "    m() {}\n"
                             "\n"
                             "    n() {}\n"
                             "}\n");
}

TEST_CASE("java-mode's format.janet names def.toplevel/def.method with correct .first markers",
          "[FormatBlankLines]") {
    const Mode mode = JavaMode();

    const std::string source = "class C {\n"
                               "    void m() {}\n"
                               "    C() {}\n"
                               "}\n"
                               "interface I {\n"
                               "    void m();\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 2);
    REQUIRE(toplevel[0].isFirst);
    REQUIRE_FALSE(toplevel[1].isFirst);

    const auto methods = CapturesNamed(captures, "def.method");
    REQUIRE(methods.size() == 3); // C's void m()/C(), and I's void m()
    REQUIRE(methods[0].isFirst);
    REQUIRE_FALSE(methods[1].isFirst);
    REQUIRE(methods[2].isFirst); // interface I's own method list starts fresh
}

TEST_CASE("End to end: blank lines applied to a real java-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.method", 1);

    const Mode mode = JavaMode();
    Buffer     buffer("test.java");
    buffer.InsertAtPoint("class C {\n"
                         "    void m() {}\n"
                         "    C() {}\n"
                         "}\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "java", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "class C {\n"
                             "    void m() {}\n"
                             "\n"
                             "    C() {}\n"
                             "}\n");
}

// go-mode: gets def.toplevel but deliberately NO def.method at all -- a
// real language difference (no nested methods), not a scope cut.
TEST_CASE("go-mode's format.janet names def.toplevel and NO def.method at all", "[FormatBlankLines]") {
    const Mode mode = GoMode();

    const std::string source = "package main\n"
                               "func f() {}\n"
                               "type T struct {\n"
                               "    X int\n"
                               "}\n"
                               "func (t T) M() {}\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 3);
    // "package main" is a real preceding sibling -- even the first
    // declaration in an ordinary Go file is not isFirst, matching the
    // same lesson python's own "import os" case already taught.
    REQUIRE_FALSE(toplevel[0].isFirst);

    REQUIRE(CapturesNamed(captures, "def.method").empty());
}

TEST_CASE("End to end: blank lines applied to a real go-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.toplevel", 1);

    const Mode mode = GoMode();
    Buffer     buffer("test.go");
    buffer.InsertAtPoint("package main\nfunc f() {}\nfunc g() {}\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "go", mode.formatCaptures(buffer.Text())));

    // minBefore applies to f() too -- "package main" is a real preceding
    // sibling (isFirst is false for f()), not exempted, matching the
    // capture-set test's own documented finding above.
    REQUIRE(buffer.Text() == "package main\n\nfunc f() {}\n\nfunc g() {}\n");
}

TEST_CASE("php-mode's format.janet names def.toplevel/def.method with correct .first markers",
          "[FormatBlankLines]") {
    const Mode mode = PhpMode();

    const std::string source = "<?php\n"
                               "function f() {}\n"
                               "class C {\n"
                               "    public function m() {}\n"
                               "    public function n() {}\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 2);
    // "<?php" is a real preceding sibling too, same lesson as go's own
    // package clause.
    REQUIRE_FALSE(toplevel[0].isFirst);

    const auto methods = CapturesNamed(captures, "def.method");
    REQUIRE(methods.size() == 2);
    REQUIRE(methods[0].isFirst);
    REQUIRE_FALSE(methods[1].isFirst);
}

TEST_CASE("php-mode's format.janet does not capture a namespace as def.toplevel", "[FormatBlankLines]") {
    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nnamespace N;\nfunction f() {}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "def.toplevel").size() == 1);
}

TEST_CASE("End to end: blank lines applied to a real php-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.method", 1);

    const Mode mode = PhpMode();
    Buffer     buffer("test.php");
    buffer.InsertAtPoint("<?php\n"
                         "class C {\n"
                         "    public function m() {}\n"
                         "    public function n() {}\n"
                         "}\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "php", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "<?php\n"
                             "class C {\n"
                             "    public function m() {}\n"
                             "\n"
                             "    public function n() {}\n"
                             "}\n");
}

TEST_CASE("rust-mode's format.janet names def.toplevel/def.method with correct .first markers",
          "[FormatBlankLines]") {
    const Mode mode = RustMode();

    const std::string source = "fn f() {}\n"
                               "impl S {\n"
                               "    fn m(&self) {}\n"
                               "    fn n(&self) {}\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 2);
    REQUIRE(toplevel[0].isFirst);
    REQUIRE_FALSE(toplevel[1].isFirst);

    const auto methods = CapturesNamed(captures, "def.method");
    REQUIRE(methods.size() == 2);
    REQUIRE(methods[0].isFirst);
    REQUIRE_FALSE(methods[1].isFirst);
}

TEST_CASE("rust-mode's format.janet treats a mod-nested function as def.method too", "[FormatBlankLines]") {
    // Deliberate scope cut, documented in format.janet: impl/trait's real
    // methods and a mod's own nested free functions share the same
    // declaration_list node type and are not distinguished.
    const Mode        mode   = RustMode();
    const std::string source = "mod m {\n    fn f() {}\n    fn g() {}\n}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "def.method").size() == 2);
}

TEST_CASE("End to end: blank lines applied to a real rust-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.method", 1);

    const Mode mode = RustMode();
    Buffer     buffer("test.rs");
    buffer.InsertAtPoint("impl S {\n"
                         "    fn m(&self) {}\n"
                         "    fn n(&self) {}\n"
                         "}\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "rust", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "impl S {\n"
                             "    fn m(&self) {}\n"
                             "\n"
                             "    fn n(&self) {}\n"
                             "}\n");
}

TEST_CASE("csharp-mode's format.janet names def.toplevel/def.method with correct .first markers",
          "[FormatBlankLines]") {
    const Mode mode = CSharpMode();

    const std::string source = "class C {\n"
                               "    void M() {}\n"
                               "    void N() {}\n"
                               "}\n"
                               "interface I {\n"
                               "    void M();\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 2);
    REQUIRE(toplevel[0].isFirst);
    REQUIRE_FALSE(toplevel[1].isFirst);

    const auto methods = CapturesNamed(captures, "def.method");
    REQUIRE(methods.size() == 3); // C's M()/N(), and I's own M()
    REQUIRE(methods[0].isFirst);
    REQUIRE_FALSE(methods[1].isFirst);
    REQUIRE(methods[2].isFirst); // interface I's own method list starts fresh
}

TEST_CASE("csharp-mode's format.janet treats a using directive as a real preceding sibling",
          "[FormatBlankLines]") {
    // Same lesson python's own leading "import os"/go's "package main"
    // already taught -- not a bug.
    const Mode        mode   = CSharpMode();
    const std::string source = "using System;\nclass C {\n}\n";
    REQUIRE_FALSE(CapturesNamed(mode.formatCaptures(source), "def.toplevel")[0].isFirst);
}

TEST_CASE("End to end: blank lines applied to a real csharp-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.method", 1);

    const Mode mode = CSharpMode();
    Buffer     buffer("test.cs");
    buffer.InsertAtPoint("class C {\n"
                         "    void M() {}\n"
                         "    void N() {}\n"
                         "}\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "csharp", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "class C {\n"
                             "    void M() {}\n"
                             "\n"
                             "    void N() {}\n"
                             "}\n");
}

TEST_CASE("typescript-mode's own format.janet widens def.toplevel to interface/enum/type-alias/"
          "abstract-class",
          "[FormatBlankLines]") {
    const Mode mode = TypeScriptMode();

    const std::string source = "interface I {\n"
                               "    m(): void;\n"
                               "}\n"
                               "enum E {\n"
                               "    A\n"
                               "}\n"
                               "type T = { x: number };\n"
                               "abstract class A {\n"
                               "    abstract m(): void;\n"
                               "}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "def.toplevel").size() == 4);
}

TEST_CASE("typescript-mode's own format.janet names def.method for an interface's own method signature",
          "[FormatBlankLines]") {
    const Mode        mode    = TypeScriptMode();
    const std::string source  = "interface I {\n    m(): void;\n    n(): void;\n}\n";
    const auto         methods = CapturesNamed(mode.formatCaptures(source), "def.method");
    REQUIRE(methods.size() == 2);
    REQUIRE(methods[0].isFirst);
    REQUIRE_FALSE(methods[1].isFirst);
}

TEST_CASE("typescript-mode's own format.janet names def.method for BOTH an abstract member and a "
          "concrete method sharing one class_body -- three distinct node types",
          "[FormatBlankLines]") {
    // method_signature/abstract_method_signature/method_definition are
    // three distinct node types (verified live), each needing its own
    // pattern; found by isFirst rather than vector position, since
    // captures from different underlying patterns are not guaranteed to
    // come back in byte order relative to each other.
    const Mode        mode   = TypeScriptMode();
    const std::string source = "abstract class A {\n"
                               "    abstract m(): void;\n"
                               "    concrete(): void {}\n"
                               "}\n";
    const auto methods = CapturesNamed(mode.formatCaptures(source), "def.method");
    REQUIRE(methods.size() == 2);

    const auto firstIt = std::find_if(methods.begin(), methods.end(), [](const FormatCapture& c) { return c.isFirst; });
    REQUIRE(firstIt != methods.end());
    REQUIRE(source.substr(firstIt->startByte, 8) == "abstract");

    const auto secondIt =
        std::find_if(methods.begin(), methods.end(), [](const FormatCapture& c) { return !c.isFirst; });
    REQUIRE(secondIt != methods.end());
    REQUIRE(source.substr(secondIt->startByte, 8) == "concrete");
}

TEST_CASE("End to end: blank lines applied to a real typescript-mode buffer, combining inherited "
          "javascript captures with typescript-only ones",
          "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.toplevel", 1);

    const Mode mode = TypeScriptMode();
    Buffer     buffer("test.ts");
    buffer.InsertAtPoint("interface I {\n"
                         "    m(): void;\n"
                         "}\n"
                         "function f(): void {}\n");

    ApplyFormatTextEdits(buffer,
                         ComputeBlankLineEdits(buffer.Text(), "typescript", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "interface I {\n"
                             "    m(): void;\n"
                             "}\n"
                             "\n"
                             "function f(): void {}\n");
}

TEST_CASE("kotlin-mode's format.janet names def.toplevel/def.method with correct .first markers",
          "[FormatBlankLines]") {
    const Mode mode = KotlinMode();

    const std::string source = "class C {\n"
                               "    fun m(): Unit {}\n"
                               "    fun n(): Unit {}\n"
                               "}\n"
                               "object O {\n"
                               "    fun m(): Unit {}\n"
                               "}\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 2);
    REQUIRE(toplevel[0].isFirst);
    REQUIRE_FALSE(toplevel[1].isFirst);

    const auto methods = CapturesNamed(captures, "def.method");
    REQUIRE(methods.size() == 3); // C's m()/n(), O's own m()
    REQUIRE(methods[0].isFirst);
    REQUIRE_FALSE(methods[1].isFirst);
    REQUIRE(methods[2].isFirst); // object O's own method list starts fresh
}

TEST_CASE("kotlin-mode's format.janet names def.method for a companion object's own methods too",
          "[FormatBlankLines]") {
    const Mode        mode   = KotlinMode();
    const std::string source = "class C {\n"
                               "    companion object {\n"
                               "        fun m(): Unit {}\n"
                               "        fun n(): Unit {}\n"
                               "    }\n"
                               "}\n";
    REQUIRE(CapturesNamed(mode.formatCaptures(source), "def.method").size() == 2);
}

TEST_CASE("End to end: blank lines applied to a real kotlin-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.method", 1);

    const Mode mode = KotlinMode();
    Buffer     buffer("test.kt");
    buffer.InsertAtPoint("class C {\n"
                         "    fun m(): Unit {}\n"
                         "    fun n(): Unit {}\n"
                         "}\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "kotlin", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "class C {\n"
                             "    fun m(): Unit {}\n"
                             "\n"
                             "    fun n(): Unit {}\n"
                             "}\n");
}

TEST_CASE("c-mode's format.janet names def.toplevel and NO def.method at all", "[FormatBlankLines]") {
    // C structs/unions hold only data fields, never functions -- a real
    // language absence, the same one go/format.janet's own file
    // documents for Go.
    const Mode mode = CMode();

    const std::string source = "int f(void) {\n"
                               "    return 1;\n"
                               "}\n"
                               "struct S {\n"
                               "    int x;\n"
                               "};\n";
    const auto captures = mode.formatCaptures(source);

    const auto toplevel = CapturesNamed(captures, "def.toplevel");
    REQUIRE(toplevel.size() == 2);
    REQUIRE(toplevel[0].isFirst);
    REQUIRE_FALSE(toplevel[1].isFirst);

    REQUIRE(CapturesNamed(captures, "def.method").empty());
}

TEST_CASE("End to end: blank lines applied to a real c-mode buffer", "[FormatBlankLines]") {
    const FormatRulesGuard guard;
    SetBlankMinBefore("def.toplevel", 1);

    const Mode mode = CMode();
    Buffer     buffer("test.c");
    buffer.InsertAtPoint("int f(void) {\n    return 1;\n}\nint g(void) {\n    return 2;\n}\n");

    ApplyFormatTextEdits(buffer, ComputeBlankLineEdits(buffer.Text(), "c", mode.formatCaptures(buffer.Text())));

    REQUIRE(buffer.Text() == "int f(void) {\n    return 1;\n}\n\nint g(void) {\n    return 2;\n}\n");
}
