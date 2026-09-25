#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"

using ned::editor::CppMode;
using ned::editor::CSharpMode;
using ned::editor::GoMode;
using ned::editor::JavaMode;
using ned::editor::JavaScriptMode;
using ned::editor::KotlinMode;
using ned::editor::PhpMode;
using ned::editor::PythonMode;
using ned::editor::TestMarker;
using ned::editor::TsxMode;
using ned::editor::TypeScriptMode;

namespace {

std::vector<std::string> MarkerNames(const std::vector<TestMarker>& markers) {
    std::vector<std::string> names;
    names.reserve(markers.size());
    for (const TestMarker& marker : markers) {
        names.push_back(marker.name);
    }
    return names;
}

} // namespace

TEST_CASE("Modes with no test query configured have an empty testDiscovery", "[TestRun]") {
    CHECK_FALSE(static_cast<bool>(ned::editor::FundamentalMode().testDiscovery));
    CHECK_FALSE(static_cast<bool>(ned::editor::JsonMode().testDiscovery));
    CHECK_FALSE(static_cast<bool>(ned::editor::ModeByName("yaml-mode")->testDiscovery));
}

TEST_CASE("CMode testDiscovery finds Unity, CMocka, Check, Criterion and greatest tests", "[TestRun]") {
    const auto mode = ned::editor::CMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    const std::string text = "static int helper(void) { return 1; }\n"
                             "void test_Add(void) {\n"
                             "    TEST_ASSERT_EQUAL(2, add(1, 1));\n"
                             "}\n"
                             "static void test_add_state(void **state) {\n"
                             "    assert_int_equal(2, add(1, 1));\n"
                             "}\n"
                             "START_TEST(test_check_add)\n"
                             "{\n"
                             "    ck_assert_int_eq(add(1, 1), 2);\n"
                             "}\n"
                             "END_TEST\n"
                             "\n"
                             "START_TEST(test_check_sub)\n"
                             "{\n"
                             "    ck_assert_int_eq(sub(2, 1), 1);\n"
                             "}\n"
                             "END_TEST\n"
                             "\n"
                             "int main(void) { return 0; }\n";

    const auto checkMarkers = mode.testDiscovery(text);
    CHECK(MarkerNames(checkMarkers) ==
          std::vector<std::string>{"test_Add", "test_add_state", "test_check_add", "test_check_sub"});
    // The second Check test fuses with the END_TEST before it; its marker
    // still starts at its own START_TEST and covers its body.
    REQUIRE(checkMarkers.size() == 4);
    CHECK(checkMarkers[3].startByte == text.find("START_TEST(test_check_sub)"));
    CHECK(checkMarkers[3].endByte > text.find("sub(2, 1)"));

    const std::string greatest = "#include \"greatest.h\"\n"
                                 "\n"
                                 "TEST add_works(void) {\n"
                                 "    ASSERT_EQ(2, add(1, 1));\n"
                                 "    PASS();\n"
                                 "}\n"
                                 "\n"
                                 "TEST sub_works(void) {\n"
                                 "    PASS();\n"
                                 "}\n";
    CHECK(MarkerNames(mode.testDiscovery(greatest)) == std::vector<std::string>{"add_works", "sub_works"});

    const std::string criterion = "#include <criterion/criterion.h>\n"
                                  "\n"
                                  "Test(math, add) {\n"
                                  "    cr_assert(add(1, 1) == 2);\n"
                                  "}\n"
                                  "\n"
                                  "Test(math, sub, .disabled = true) {\n"
                                  "    cr_assert(sub(2, 1) == 1);\n"
                                  "}\n";
    const auto        markers   = mode.testDiscovery(criterion);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"add", "sub"});
    // The body is a sibling of the call, as with Catch2 in C++.
    const std::size_t insideBody = criterion.find("cr_assert");
    REQUIRE_FALSE(markers.empty());
    CHECK(markers[0].startByte < insideBody);
    CHECK(insideBody < markers[0].endByte);
}

TEST_CASE("CppMode testDiscovery finds Catch2 and gtest definitions", "[TestRun]") {
    const auto mode = CppMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    const std::string text = "#include <catch2/catch_test_macros.hpp>\n"
                             "\n"
                             "TEST_CASE(\"Addition works\") {\n"
                             "    CHECK(1 + 1 == 2);\n"
                             "}\n"
                             "\n"
                             "TEST_CASE(\"With tags\", \"[math]\") {\n"
                             "    CHECK(true);\n"
                             "}\n"
                             "\n"
                             "SCENARIO(\"A scenario\") {\n"
                             "    CHECK(true);\n"
                             "}\n"
                             "\n"
                             "TEST_CASE_METHOD(Fixture, \"Method case\") {\n"
                             "    CHECK(true);\n"
                             "}\n"
                             "\n"
                             "TEST(SuiteName, CaseName) {\n"
                             "    int x = 1;\n"
                             "}\n"
                             "\n"
                             "TEST_F(FixtureName, FixtureCase) {\n"
                             "    int y = 2;\n"
                             "}\n";

    const auto markers = mode.testDiscovery(text);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"Addition works", "With tags", "A scenario", "Method case",
                                                           "CaseName", "FixtureCase"});

    // The Catch2 macro's body parses as a *sibling* compound_statement
    // (unexpanded macros aren't valid C++) -- the marker must still cover
    // it, so run-test-at-point resolves from inside the body.
    const std::size_t insideFirstBody = text.find("CHECK(1 + 1 == 2)");
    REQUIRE(markers[0].startByte == text.find("TEST_CASE(\"Addition works\")"));
    CHECK(markers[0].startByte < insideFirstBody);
    CHECK(insideFirstBody < markers[0].endByte);

    // gtest's function_definition shape includes its body natively.
    const std::size_t insideGtestBody = text.find("int x = 1;");
    CHECK(markers[4].startByte < insideGtestBody);
    CHECK(insideGtestBody < markers[4].endByte);
}

TEST_CASE("CSharpMode testDiscovery finds xUnit/NUnit/MSTest attributes, but not an unrelated helper method",
          "[TestRun]") {
    const auto mode = CSharpMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    const std::string text = "public class WidgetTests {\n"
                             "    [Fact]\n"
                             "    public void AddWorks() {\n"
                             "        Assert.Equal(2, 1 + 1);\n"
                             "    }\n"
                             "\n"
                             "    [Theory]\n"
                             "    [InlineData(1, 2)]\n"
                             "    public void AddWorksTheory(int a, int b) {\n"
                             "    }\n"
                             "\n"
                             "    [TestMethod]\n"
                             "    public void MSTestStyle() {\n"
                             "    }\n"
                             "\n"
                             "    public void HelperSetup() {\n"
                             "    }\n"
                             "}\n";

    const auto markers = mode.testDiscovery(text);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"AddWorks", "AddWorksTheory", "MSTestStyle"});

    const std::size_t insideBody = text.find("Assert.Equal");
    REQUIRE(markers[0].startByte == text.find("[Fact]"));
    CHECK(markers[0].startByte < insideBody);
    CHECK(insideBody < markers[0].endByte);
}

TEST_CASE("JavaMode testDiscovery finds JUnit's @Test and its variants, but not an unrelated helper method",
          "[TestRun]") {
    const auto mode = JavaMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    // Both annotation node types are covered: a bare "@Test" is a
    // marker_annotation, "@Test(expected = ...)" an annotation -- see
    // java-tests.scm's own header comment.
    const std::string text = "class WidgetTests {\n"
                             "    @Test\n"
                             "    public void addWorks() {\n"
                             "        assertEquals(2, 1 + 1);\n"
                             "    }\n"
                             "\n"
                             "    @Test(expected = IllegalStateException.class)\n"
                             "    public void addThrows() {\n"
                             "    }\n"
                             "\n"
                             "    @ParameterizedTest\n"
                             "    @ValueSource(ints = {1, 2})\n"
                             "    void addWorksParameterized(int a) {\n"
                             "    }\n"
                             "\n"
                             "    void helperSetup() {\n"
                             "    }\n"
                             "}\n";

    const auto markers = mode.testDiscovery(text);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"addWorks", "addThrows", "addWorksParameterized"});

    const std::size_t insideBody = text.find("assertEquals");
    CHECK(markers[0].startByte < insideBody);
    CHECK(insideBody < markers[0].endByte);
}

TEST_CASE("KotlinMode testDiscovery finds kotlin.test/JUnit annotations, but not an unrelated helper function",
          "[TestRun]") {
    const auto mode = KotlinMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    // Both annotation shapes are covered: a bare "@Test" carries a
    // user_type, "@Test(expected = ...)" a constructor_invocation wrapping
    // one -- see kotlin-tests.scm's own header comment.
    const std::string text = "class WidgetTests {\n"
                             "    @Test\n"
                             "    fun addWorks() {\n"
                             "        assertEquals(2, 1 + 1)\n"
                             "    }\n"
                             "\n"
                             "    @Test(expected = IllegalStateException::class)\n"
                             "    fun addThrows() {\n"
                             "    }\n"
                             "\n"
                             "    @ParameterizedTest\n"
                             "    fun addWorksParameterized(a: Int) {\n"
                             "    }\n"
                             "\n"
                             "    fun helperSetup() {\n"
                             "    }\n"
                             "}\n";

    const auto markers = mode.testDiscovery(text);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"addWorks", "addThrows", "addWorksParameterized"});

    const std::size_t insideBody = text.find("assertEquals");
    CHECK(markers[0].startByte < insideBody);
    CHECK(insideBody < markers[0].endByte);
}

TEST_CASE("GoMode testDiscovery finds Test/Benchmark/Fuzz/Example functions, but not an unrelated helper",
          "[TestRun]") {
    const auto mode = GoMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    const std::string text = "package widget\n"
                             "\n"
                             "import \"testing\"\n"
                             "\n"
                             "func TestAdd(t *testing.T) {\n"
                             "    if 1+1 != 2 {\n"
                             "        t.Fail()\n"
                             "    }\n"
                             "}\n"
                             "\n"
                             "func BenchmarkAdd(b *testing.B) {\n"
                             "}\n"
                             "\n"
                             "func FuzzAdd(f *testing.F) {\n"
                             "}\n"
                             "\n"
                             "func ExampleAdd() {\n"
                             "    // Output: 2\n"
                             "}\n"
                             "\n"
                             "func TestHelperSetup() int {\n"
                             "    return 0\n"
                             "}\n";

    const auto markers = mode.testDiscovery(text);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"TestAdd", "BenchmarkAdd", "FuzzAdd", "ExampleAdd"});

    const std::size_t insideBody = text.find("t.Fail()");
    REQUIRE(markers[0].startByte == text.find("func TestAdd"));
    CHECK(markers[0].startByte < insideBody);
    CHECK(insideBody < markers[0].endByte);
}

TEST_CASE("PythonMode testDiscovery finds test functions, methods, and Test classes", "[TestRun]") {
    const auto mode = PythonMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    const std::string text = "import pytest\n"
                             "\n"
                             "def test_top_level():\n"
                             "    assert True\n"
                             "\n"
                             "def helper():\n"
                             "    pass\n"
                             "\n"
                             "class TestThings:\n"
                             "    def test_method(self):\n"
                             "        assert True\n"
                             "\n"
                             "    def not_a_test(self):\n"
                             "        pass\n";

    const auto markers = mode.testDiscovery(text);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"test_top_level", "TestThings", "test_method"});

    // The class marker contains the method marker -- innermost-wins
    // resolution is what makes run-test-at-point pick the method.
    CHECK(markers[1].startByte < markers[2].startByte);
    CHECK(markers[2].endByte <= markers[1].endByte);
}

TEST_CASE("JavaScript/TypeScript testDiscovery finds it/test/describe with modifiers, quotes stripped", "[TestRun]") {
    const std::string text = "describe('math', () => {\n"
                             "  it('adds', () => {\n"
                             "    expect(1 + 1).toBe(2);\n"
                             "  });\n"
                             "  it.only(\"focused case\", () => {});\n"
                             "  test('another', () => {});\n"
                             "});\n"
                             "notATest('nope', () => {});\n";

    for (const auto& mode : {JavaScriptMode(), TypeScriptMode(), TsxMode()}) {
        REQUIRE(static_cast<bool>(mode.testDiscovery));
        const auto markers = mode.testDiscovery(text);
        CHECK(MarkerNames(markers) == std::vector<std::string>{"math", "adds", "focused case", "another"});

        // describe's call_expression range includes the arrow-function body,
        // so the nested its sit inside it (innermost-wins resolution).
        CHECK(markers[0].startByte < markers[1].startByte);
        CHECK(markers[1].endByte < markers[0].endByte);
    }
}

TEST_CASE("PhpMode testDiscovery finds test methods, #[Test] attributes, and Test classes", "[TestRun]") {
    const auto mode = PhpMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    const std::string text = "<?php\n"
                             "class FooTest extends TestCase {\n"
                             "    public function testBar(): void {\n"
                             "        $this->assertTrue(true);\n"
                             "    }\n"
                             "    #[Test]\n"
                             "    public function checksThings(): void {}\n"
                             "    private function helper(): void {}\n"
                             "}\n";

    const auto markers = mode.testDiscovery(text);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"FooTest", "testBar", "checksThings"});
}

TEST_CASE("PhpMode testDiscovery names a doubly-matched method exactly once", "[TestRun]") {
    // A method that is both test*-named and #[Test]-attributed matches two
    // patterns with identical ranges -- the closure's dedupe keeps one.
    const auto        mode    = PhpMode();
    const std::string text    = "<?php\n"
                                "class DoubleTest {\n"
                                "    #[Test]\n"
                                "    public function testBoth(): void {}\n"
                                "}\n";
    const auto        markers = mode.testDiscovery(text);
    CHECK(MarkerNames(markers) == std::vector<std::string>{"DoubleTest", "testBoth"});
}

namespace {

std::vector<std::string> DescribeTestMarkers(const std::vector<TestMarker>& markers) {
    std::vector<std::string> out;
    out.reserve(markers.size());
    for (const TestMarker& marker : markers) {
        out.push_back("[" + std::to_string(marker.startByte) + "," + std::to_string(marker.endByte) + ") " + marker.name);
    }
    return out;
}

} // namespace

// per-subtree-fact-memoization follow-up: the property Mode.cpp's testDiscovery
// closure's own MatchCache wiring exists for -- calling the SAME Mode
// instance's testDiscovery repeatedly across an evolving sequence of edits
// (sharing one MatchCache under the hood) must report byte-for-byte the same
// thing a completely FRESH Mode would report on that exact text, at every
// step, not just the final one. Uses JS's describe/it nesting so the
// smallest-enclosing-definition correlation is actually exercised, not just
// the flat capture collection.
TEST_CASE("JavaScriptMode testDiscovery stays correct across a sequence of incremental edits", "[TestRun]") {
    const auto mode = JavaScriptMode();
    REQUIRE(static_cast<bool>(mode.testDiscovery));

    const std::vector<std::string> steps = {
        "describe('math', () => {\n"
        "  it('adds', () => {});\n"
        "});\n",
        "describe('math', () => {\n"
        "  it('adds', () => {});\n"
        "  it('subtracts', () => {});\n"
        "});\n",
        "describe('math', () => {\n"
        "  it('adds', () => {});\n"
        "  it('subtracts', () => {});\n"
        "});\n"
        "describe('strings', () => {\n"
        "  it('concatenates', () => {});\n"
        "});\n",
        // Renames 'adds' -> 'sums', a small localized edit deep inside
        // otherwise-unaffected content on both sides.
        "describe('math', () => {\n"
        "  it('sums', () => {});\n"
        "  it('subtracts', () => {});\n"
        "});\n"
        "describe('strings', () => {\n"
        "  it('concatenates', () => {});\n"
        "});\n",
    };

    for (std::size_t i = 0; i < steps.size(); ++i) {
        INFO("step " << i << ": " << steps[i]);
        const auto incremental = mode.testDiscovery(steps[i]);
        const auto fresh       = JavaScriptMode().testDiscovery(steps[i]);
        REQUIRE(DescribeTestMarkers(incremental) == DescribeTestMarkers(fresh));
    }
}

namespace {

std::vector<std::string> DiscoveredNames(const std::string& modeName, const std::string& text) {
    const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName(modeName);
    REQUIRE(mode.has_value());
    REQUIRE(static_cast<bool>(mode->testDiscovery));
    return MarkerNames(mode->testDiscovery(text));
}

} // namespace

TEST_CASE("Ruby, Elixir, Swift, Scala, Dart and Haskell discover their frameworks' tests", "[TestRun]") {
    using V = std::vector<std::string>;
    CHECK(DiscoveredNames("ruby-mode", "RSpec.describe Calculator do\n"
                                       "  context \"adding\" do\n"
                                       "    it \"sums\" do\n"
                                       "      expect(1).to eq(1)\n"
                                       "    end\n"
                                       "  end\n"
                                       "end\n"
                                       "class CalcTest < Minitest::Test\n"
                                       "  def test_add\n"
                                       "  end\n"
                                       "  def helper\n"
                                       "  end\n"
                                       "end\n") == V{"Calculator", "adding", "sums", "CalcTest", "test_add"});

    CHECK(DiscoveredNames("elixir-mode", "defmodule CalcTest do\n"
                                         "  use ExUnit.Case\n"
                                         "  describe \"add\" do\n"
                                         "    test \"sums\", %{a: a} do\n"
                                         "      assert 1 == 1\n"
                                         "    end\n"
                                         "  end\n"
                                         "  test \"plain\" do\n"
                                         "  end\n"
                                         "end\n") == V{"add", "sums", "plain"});

    CHECK(DiscoveredNames("swift-mode", "final class CalcTests: XCTestCase {\n"
                                        "  func testAdd() throws {}\n"
                                        "  func helper() {}\n"
                                        "}\n"
                                        "@Test func adds() {}\n"
                                        "@Suite struct Math {\n"
                                        "  @Test(\"named\") func named() {}\n"
                                        "}\n") == V{"CalcTests", "testAdd", "adds", "Math", "named"});

    CHECK(DiscoveredNames("scala-mode", "class CalcSuite extends AnyFunSuite {\n"
                                        "  test(\"adds\") {\n"
                                        "    assert(1 == 1)\n"
                                        "  }\n"
                                        "}\n"
                                        "class CalcSpec extends AnyFlatSpec {\n"
                                        "  \"Calc\" should \"add\" in {}\n"
                                        "  it should \"subtract\" in {}\n"
                                        "}\n") == V{"adds", "add", "subtract"});

    CHECK(DiscoveredNames("dart-mode", "void main() {\n"
                                       "  group('calc', () {\n"
                                       "    test('adds', () {\n"
                                       "      expect(1, 1);\n"
                                       "    });\n"
                                       "  });\n"
                                       "  testWidgets('renders', (tester) async {});\n"
                                       "}\n") == V{"calc", "adds", "renders"});

    CHECK(DiscoveredNames("haskell-mode", "spec :: Spec\n"
                                          "spec = describe \"calc\" $ do\n"
                                          "  it \"adds\" $ do\n"
                                          "    1 `shouldBe` 1\n"
                                          "\n"
                                          "tests = testGroup \"group\" [ testCase \"one\" $ 1 @?= 1 ]\n") ==
          V{"calc", "adds", "group", "one"});
}

TEST_CASE("Scripting, functional and systems languages discover their frameworks' tests", "[TestRun]") {
    using V = std::vector<std::string>;
    CHECK(DiscoveredNames("lua-mode", "describe(\"calc\", function()\n"
                                      "  it(\"adds\", function() end)\n"
                                      "end)\n"
                                      "function TestCalc:testSub() end\n"
                                      "function testPlain() end\n"
                                      "function helper() end\n") == V{"calc", "adds", "testSub", "testPlain"});

    CHECK(DiscoveredNames("groovy-mode", "class CalcSpec extends Specification {\n"
                                         "  def \"adds numbers\"() { expect: 1 == 1 }\n"
                                         "  @Test void plainTest() {}\n"
                                         "  void helper() {}\n"
                                         "}\n") == V{"adds numbers", "plainTest"});

    CHECK(DiscoveredNames("crystal-mode", "describe Calc do\n"
                                          "  it \"adds\" do\n"
                                          "  end\n"
                                          "end\n") == V{"Calc", "adds"});

    CHECK(DiscoveredNames("julia-mode", "@testset \"calc\" begin\n"
                                        "  @test 1 == 1\n"
                                        "end\n") == V{"calc"});

    CHECK(DiscoveredNames("r-mode", "describe(\"calc\", {\n"
                                    "  it(\"adds\", { expect_equal(1, 1) })\n"
                                    "})\n"
                                    "test_that(\"subtracts\", { expect_equal(0, 0) })\n") ==
          V{"calc", "adds", "subtracts"});

    CHECK(DiscoveredNames("perl-mode", "subtest \"adds\" => sub { ok(1) };\n"
                                       "subtest('subtracts', sub { ok(1) });\n") == V{"adds", "subtracts"});

    CHECK(DiscoveredNames("clojure-mode", "(deftest adds\n"
                                          "  (testing \"small numbers\" (is (= 2 (+ 1 1)))))\n"
                                          "(defn helper [] 1)\n") == V{"adds", "small numbers"});

    CHECK(DiscoveredNames("erlang-mode", "add_test() -> ok.\n"
                                         "add_gen_test_() -> [].\n"
                                         "helper() -> ok.\n") == V{"add_test", "add_gen_test_"});

    CHECK(DiscoveredNames("nim-mode", "suite \"calc\":\n"
                                      "  test \"adds\":\n"
                                      "    check 1 == 1\n") == V{"calc", "adds"});

    CHECK(DiscoveredNames("gleam-mode", "pub fn add_test() { 1 }\n"
                                        "pub fn helper() { 1 }\n") == V{"add_test"});

    CHECK(DiscoveredNames("ocaml-mode", "let tests = [ test_case \"adds\" `Quick f ]\n"
                                        "let%expect_test \"prints\" = ()\n"
                                        "let%test \"truth\" = true\n") == V{"adds", "prints", "truth"});

    CHECK(DiscoveredNames("fsharp-mode", "let tests = testList \"calc\" [ testCase \"adds\" <| fun () -> () ]\n"
                                         "[<Fact>]\n"
                                         "let ``subtracts numbers`` () = ()\n"
                                         "[<Test>]\n"
                                         "let multiplies () = ()\n") ==
          V{"calc", "adds", "subtracts numbers", "multiplies"});

    CHECK(DiscoveredNames("powershell-mode", "Describe \"calc\" {\n"
                                             "  Context \"add\" {\n"
                                             "    It \"sums\" { 1 | Should -Be 1 }\n"
                                             "  }\n"
                                             "}\n") == V{"calc", "add", "sums"});

    CHECK(DiscoveredNames("janet-mode", "(deftest one (is true))\n"
                                        "(deftest \"two\" (test 1 1))\n"
                                        "(deftest: stateful \"three\" [c] 1)\n"
                                        "(deftest (is true))\n"
                                        "(test (+ 1 2) 3)\n") == V{"one", "two", "three"});

    CHECK(DiscoveredNames("fennel-mode", "(deftest adds (testing \"x\" 1))\n"
                                         "(describe \"stack\" (fn [] (it \"pushes\" (fn [] 1))))\n"
                                         "(fn test-sub [] 1)\n"
                                         "(fn helper [] 1)\n"
                                         "{: test-sub}\n") == V{"adds", "stack", "pushes", "test-sub"});

    CHECK(DiscoveredNames("scheme-mode", "(test-begin \"calc\")\n"
                                         "(test-assert \"adds\" (= 2 (+ 1 1)))\n"
                                         "(test-assert (= 2 2))\n"
                                         "(test-equal \"sums\" 2 (+ 1 1))\n"
                                         "(test-equal \"2\" (number->string 2))\n"
                                         "(test-approximate \"near\" 1.0 1.01 0.1)\n"
                                         "(test-group \"nested\" (test-error \"fails\" #t (car '())))\n"
                                         "(test-end \"calc\")\n") ==
          V{"calc", "adds", "sums", "near", "nested", "fails"});

    CHECK(DiscoveredNames("matlab-mode", "classdef CalcTest < matlab.unittest.TestCase\n"
                                         "    methods (Test, TestTags = {'Unit'})\n"
                                         "        function adds(testCase)\n"
                                         "        end\n"
                                         "    end\n"
                                         "    methods (TestMethodSetup)\n"
                                         "        function setup(testCase)\n"
                                         "        end\n"
                                         "    end\n"
                                         "end\n") == V{"adds"});

    CHECK(DiscoveredNames("matlab-mode", "function tests = calcTest\n"
                                         "tests = functiontests(localfunctions);\n"
                                         "end\n"
                                         "function testAdd(testCase)\n"
                                         "end\n"
                                         "function subTest(testCase)\n"
                                         "end\n"
                                         "function setup(testCase)\n"
                                         "end\n") == V{"testAdd", "subTest"});

    CHECK(DiscoveredNames("matlab-mode", "function r = latest(x)\n"
                                         "r = x;\n"
                                         "end\n"
                                         "function y = contest(x)\n"
                                         "y = x;\n"
                                         "end\n")
              .empty());

    CHECK(DiscoveredNames("sql-mode", "CREATE OR REPLACE FUNCTION test_user() RETURNS SETOF TEXT AS $$\n"
                                      "  SELECT is(1, 1, 'one');\n"
                                      "$$ LANGUAGE sql;\n"
                                      "CREATE FUNCTION tap.Test_Nick() RETURNS SETOF TEXT AS $$ SELECT 1; $$ LANGUAGE sql;\n"
                                      "CREATE FUNCTION setup_users() RETURNS SETOF TEXT AS $$ SELECT 1; $$ LANGUAGE sql;\n") ==
          V{"test_user", "Test_Nick"});

    CHECK(DiscoveredNames("bash-mode", "setup() { x=1; }\n"
                                       "@test \"adds numbers\" {\n"
                                       "  run echo 1\n"
                                       "}\n"
                                       "@test subtracts {\n"
                                       "  true\n"
                                       "}\n") == V{"adds numbers", "subtracts"});

    CHECK(DiscoveredNames("racket-mode", "#lang racket\n"
                                         "(define s (test-suite \"calc\"))\n"
                                         "(test-case \"adds\" (check-equal? 1 1))\n") == V{"calc", "adds"});

    CHECK(DiscoveredNames("commonlisp-mode", "(test adds (is (= 1 1)))\n"
                                             "(define-test sums (true t))\n"
                                             "(defun helper () 1)\n") == V{"adds", "sums"});
}

TEST_CASE("Compiled and markup-adjacent languages discover their frameworks' tests", "[TestRun]") {
    using V = std::vector<std::string>;
    CHECK(DiscoveredNames("v-mode", "fn test_add() { assert 1 == 1 }\n"
                                    "fn helper() {}\n") == V{"test_add"});

    CHECK(DiscoveredNames("odin-mode", "package calc\n"
                                       "@(test)\n"
                                       "test_add :: proc(t: ^testing.T) {}\n"
                                       "helper :: proc() {}\n") == V{"test_add"});

    CHECK(DiscoveredNames("solidity-mode", "contract CalcTest is Test {\n"
                                           "  function test_Add() public {}\n"
                                           "  function testFuzz_Add(uint x) public {}\n"
                                           "  function invariant_Total() public {}\n"
                                           "  function helper() internal {}\n"
                                           "}\n") == V{"test_Add", "testFuzz_Add", "invariant_Total"});

    CHECK(DiscoveredNames("objc-mode", "@implementation CalcTests\n"
                                       "- (void)testAdd { }\n"
                                       "- (void)helper { }\n"
                                       "@end\n") == V{"testAdd"});

    CHECK(DiscoveredNames("elm-mode", "suite = describe \"calc\" [ test \"adds\" <| \\_ -> Expect.equal 1 1 ]\n") ==
          V{"calc", "adds"});

    CHECK(DiscoveredNames("purescript-mode", "spec = describe \"calc\" do\n"
                                             "  it \"adds\" do\n"
                                             "    pure unit\n") == V{"calc", "adds"});

    CHECK(DiscoveredNames("tcl-mode", "test add-1.1 {adds} -body { expr 1 } -result 1\n"
                                      "proc helper {} {}\n") == V{"add-1.1"});
}
