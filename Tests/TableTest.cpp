#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Table.h"

using ned::editor::table::Alignment;
using ned::editor::table::ComputeColumnWidths;
using ned::editor::table::FindTableBlockLines;
using ned::editor::table::PadCell;
using ned::editor::table::SplitRow;

TEST_CASE("SplitRow splits a line with edge pipes into trimmed cells", "[Table]") {
    const auto cells = SplitRow("| Name  | Age |");
    REQUIRE(cells == std::vector<std::string>{"Name", "Age"});
}

TEST_CASE("SplitRow splits a line with no edge pipes the same way", "[Table]") {
    const auto cells = SplitRow("Name  | Age");
    REQUIRE(cells == std::vector<std::string>{"Name", "Age"});
}

TEST_CASE("SplitRow tolerates a leading-only or trailing-only pipe", "[Table]") {
    REQUIRE(SplitRow("| Name | Age") == std::vector<std::string>{"Name", "Age"});
    REQUIRE(SplitRow("Name | Age |") == std::vector<std::string>{"Name", "Age"});
}

TEST_CASE("SplitRow handles an empty cell between two pipes", "[Table]") {
    REQUIRE(SplitRow("| a || b |") == std::vector<std::string>{"a", "", "b"});
}

TEST_CASE("ComputeColumnWidths takes the max width per column across all rows", "[Table]") {
    const std::vector<std::vector<std::string>> rows{{"a", "bb"}, {"ccc", "d"}};
    REQUIRE(ComputeColumnWidths(rows) == std::vector<std::size_t>{3, 2});
}

TEST_CASE("ComputeColumnWidths tolerates ragged rows", "[Table]") {
    const std::vector<std::vector<std::string>> rows{{"a", "bb", "ccc"}, {"d"}};
    REQUIRE(ComputeColumnWidths(rows) == std::vector<std::size_t>{1, 2, 3});
}

TEST_CASE("ComputeColumnWidths counts codepoints, not bytes", "[Table]") {
    // "café" is 4 codepoints but 5 bytes (the é is 2 bytes in UTF-8).
    const std::vector<std::vector<std::string>> rows{{"caf\xc3\xa9"}};
    REQUIRE(ComputeColumnWidths(rows) == std::vector<std::size_t>{4});
}

TEST_CASE("PadCell left-aligns by default, padding on the right", "[Table]") {
    REQUIRE(PadCell("ab", 5, Alignment::Default) == "ab   ");
    REQUIRE(PadCell("ab", 5, Alignment::Left) == "ab   ");
}

TEST_CASE("PadCell right-aligns, padding on the left", "[Table]") {
    REQUIRE(PadCell("ab", 5, Alignment::Right) == "   ab");
}

TEST_CASE("PadCell centers, splitting padding with the extra column on the right", "[Table]") {
    REQUIRE(PadCell("ab", 5, Alignment::Center) == " ab  ");
}

TEST_CASE("PadCell never truncates text already at or past the target width", "[Table]") {
    REQUIRE(PadCell("abcdef", 3, Alignment::Left) == "abcdef");
    REQUIRE(PadCell("abc", 3, Alignment::Right) == "abc");
}

TEST_CASE("FindTableBlockLines returns nullopt off a table line", "[Table]") {
    REQUIRE_FALSE(FindTableBlockLines("plain text\n| a | b |\n", 0).has_value());
}

TEST_CASE("FindTableBlockLines returns nullopt for an out-of-range line", "[Table]") {
    REQUIRE_FALSE(FindTableBlockLines("| a |\n", 5).has_value());
}

TEST_CASE("FindTableBlockLines finds the contiguous block surrounding pointLine", "[Table]") {
    const std::string text  = "before\n| a | b |\n| c | d |\n| e | f |\nafter\n";
    const auto        block = FindTableBlockLines(text, 2); // the middle table row
    REQUIRE(block.has_value());
    REQUIRE(block->first == 1);
    REQUIRE(block->second == 4);
}

TEST_CASE("FindTableBlockLines finds a single-line table", "[Table]") {
    const std::string text  = "before\n| a | b |\nafter\n";
    const auto        block = FindTableBlockLines(text, 1);
    REQUIRE(block.has_value());
    REQUIRE(block->first == 1);
    REQUIRE(block->second == 2);
}

TEST_CASE("FindTableBlockLines handles a table at the very start and end of the buffer", "[Table]") {
    const std::string text  = "| a |\n| b |";
    const auto        block = FindTableBlockLines(text, 0);
    REQUIRE(block.has_value());
    REQUIRE(block->first == 0);
    REQUIRE(block->second == 2);
}

TEST_CASE("FindTableBlockLines tolerates leading whitespace before the pipe", "[Table]") {
    const std::string text  = "  | indented |\n";
    const auto        block = FindTableBlockLines(text, 0);
    REQUIRE(block.has_value());
    REQUIRE(block->first == 0);
    REQUIRE(block->second == 1);
}
