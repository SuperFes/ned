#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Mode.h"

using ned::editor::CppMode;
using ned::editor::JavaScriptMode;
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
    CHECK_FALSE(static_cast<bool>(ned::editor::CMode().testDiscovery));
    CHECK_FALSE(static_cast<bool>(ned::editor::BashMode().testDiscovery));
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
