#include <catch2/catch_test_macros.hpp>

#include "Editor/IndentDetect.h"

using ned::editor::DetectedIndentKind;
using ned::editor::DetectIndentStyle;

TEST_CASE("DetectIndentStyle reports Unknown for text with no indented line at all", "[IndentDetect]") {
    REQUIRE(DetectIndentStyle("").kind == DetectedIndentKind::Unknown);
    REQUIRE(DetectIndentStyle("int main() {}\n").kind == DetectedIndentKind::Unknown);
    // Pure whitespace lines carry no indentation information -- they indent
    // nothing themselves.
    REQUIRE(DetectIndentStyle("   \n\t\n").kind == DetectedIndentKind::Unknown);
}

TEST_CASE("DetectIndentStyle reports Tabs when every indented line starts with a tab", "[IndentDetect]") {
    const auto result = DetectIndentStyle("func f() {\n\treturn 1\n}\n");
    REQUIRE(result.kind == DetectedIndentKind::Tabs);
}

TEST_CASE("DetectIndentStyle reports Spaces and its own smallest observed indent width", "[IndentDetect]") {
    const auto twoSpace = DetectIndentStyle("def f():\n  return 1\n  # comment\n");
    REQUIRE(twoSpace.kind == DetectedIndentKind::Spaces);
    REQUIRE(twoSpace.spacesWidth == 2);

    const auto fourSpace = DetectIndentStyle("void f() {\n    int x = 1;\n    return x;\n}\n");
    REQUIRE(fourSpace.kind == DetectedIndentKind::Spaces);
    REQUIRE(fourSpace.spacesWidth == 4);
}

TEST_CASE("DetectIndentStyle takes the SMALLEST nonzero space run as the width, not the first line seen",
          "[IndentDetect]") {
    // Second-level indentation (8 spaces) appears first in document order --
    // the first, shallower level (4) is still the real width.
    const auto result = DetectIndentStyle("if (x) {\n        deep();\n    }\n    shallow();\n");
    REQUIRE(result.kind == DetectedIndentKind::Spaces);
    REQUIRE(result.spacesWidth == 4);
}

TEST_CASE("DetectIndentStyle tolerates a small minority of the other character as still-dominant, not Mixed",
          "[IndentDetect]") {
    // 9 space-led lines, 1 tab-led line -- well under the 1:4 threshold.
    std::string text;
    for (int i = 0; i < 9; ++i) {
        text += "    line();\n";
    }
    text += "\tstray();\n";
    const auto result = DetectIndentStyle(text);
    REQUIRE(result.kind == DetectedIndentKind::Spaces);
}

TEST_CASE("DetectIndentStyle reports Mixed when both conventions appear at genuinely comparable counts",
          "[IndentDetect]") {
    std::string text;
    for (int i = 0; i < 5; ++i) {
        text += "    spaced();\n";
    }
    for (int i = 0; i < 5; ++i) {
        text += "\ttabbed();\n";
    }
    REQUIRE(DetectIndentStyle(text).kind == DetectedIndentKind::Mixed);
}

TEST_CASE("DetectIndentStyle counts a line by which character its leading run STARTS with", "[IndentDetect]") {
    // A tab followed by spaces still renders as a tab-led line.
    const auto result = DetectIndentStyle("func f() {\n\t    return 1\n}\n");
    REQUIRE(result.kind == DetectedIndentKind::Tabs);
}
