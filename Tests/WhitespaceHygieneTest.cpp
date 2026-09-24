#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "Text/WhitespaceHygiene.h"

using ned::text::CollapseBlankLineRuns;
using ned::text::EnsureTrailingNewline;
using ned::text::TrimTrailingWhitespaceAndBlankLines;

TEST_CASE("TrimTrailingWhitespaceAndBlankLines strips per-line trailing whitespace", "[WhitespaceHygiene]") {
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("a  \nb\t\t\nc") == "a\nb\nc");
}

TEST_CASE("TrimTrailingWhitespaceAndBlankLines collapses trailing blank lines to nothing",
          "[WhitespaceHygiene]") {
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("a\n\n\n") == "a");
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("a\n   \n\t\n") == "a");
}

TEST_CASE("TrimTrailingWhitespaceAndBlankLines leaves interior blank lines alone", "[WhitespaceHygiene]") {
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("a\n\n\nb") == "a\n\n\nb");
}

TEST_CASE("TrimTrailingWhitespaceAndBlankLines is a no-op on already-clean or empty content",
          "[WhitespaceHygiene]") {
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("a\nb\nc") == "a\nb\nc");
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("") == "");
}

TEST_CASE("EnsureTrailingNewline appends '\\n' only when missing and content is non-empty",
          "[WhitespaceHygiene]") {
    REQUIRE(EnsureTrailingNewline("a") == "a\n");
    REQUIRE(EnsureTrailingNewline("a\n") == "a\n");
    REQUIRE(EnsureTrailingNewline("") == "");
}

TEST_CASE("CollapseBlankLineRuns caps an interior run at maxConsecutive", "[WhitespaceHygiene]") {
    REQUIRE(CollapseBlankLineRuns("a\n\n\n\nb", 2) == "a\n\n\nb"); // 3 blank lines -> 2
    REQUIRE(CollapseBlankLineRuns("a\n\n\n\nb", 0) == "a\nb"); // 0 allowed -> all dropped
    REQUIRE(CollapseBlankLineRuns("a\n\nb", 2) == "a\n\nb"); // already within the cap -- untouched
}

TEST_CASE("CollapseBlankLineRuns treats a whitespace-only line as blank", "[WhitespaceHygiene]") {
    // Classifies a whitespace-only line as blank for run-counting purposes,
    // but doesn't itself trim -- the surviving line ("  ", kept since it's
    // first in the run) keeps its original content verbatim. Trimming is
    // TrimTrailingWhitespaceAndBlankLines's job, applied earlier in the
    // Hygiene pipeline (Editor/Format.cpp).
    REQUIRE(CollapseBlankLineRuns("a\n  \n\t\n\nb", 1) == "a\n  \nb");
}

TEST_CASE("CollapseBlankLineRuns handles a run at the very start of the document", "[WhitespaceHygiene]") {
    REQUIRE(CollapseBlankLineRuns("\n\n\nx", 2) == "\n\nx");
}

TEST_CASE("CollapseBlankLineRuns handles a run at the very end of the document", "[WhitespaceHygiene]") {
    REQUIRE(CollapseBlankLineRuns("a\n\n\n", 2) == "a\n\n");
}

TEST_CASE("CollapseBlankLineRuns with a negative maxConsecutive is a no-op (the disabled sentinel)",
          "[WhitespaceHygiene]") {
    REQUIRE(CollapseBlankLineRuns("a\n\n\n\nb", -1) == "a\n\n\n\nb");
}

TEST_CASE("CollapseBlankLineRuns is a no-op on empty content", "[WhitespaceHygiene]") {
    REQUIRE(CollapseBlankLineRuns("", 2).empty());
}

TEST_CASE("Kept lines keep their trailing whitespace and survive the collapses", "[WhitespaceHygiene]") {
    const std::vector<std::size_t> keep{0, 3, 4, 5};
    // Line 0 is a kept hard break; line 1's trailing space is noise.
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("a  \nb \n", keep) == "a  \nb");
    // A kept blank run (a code block's own lines) is neither trimmed nor collapsed.
    REQUIRE(CollapseBlankLineRuns("x\n\n\n  \n\n\ny", 1, keep) == "x\n\n  \n\n\ny");
    // A kept blank line at the end stays, the unkept one after it goes.
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("```\ncode\n```\n  \n  \n", std::vector<std::size_t>{3}) == "```\ncode\n```\n  \n");
    // An unclosed fence's trailing blank line is its content.
    REQUIRE(TrimTrailingWhitespaceAndBlankLines("```\n\n", std::vector<std::size_t>{1}) == "```\n\n");
}
