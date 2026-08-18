#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Markdown.h"
#include "Editor/Table.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::markdown::AlignTableAtPoint;
using ned::editor::markdown::FindTableAtPoint;
using ned::editor::table::Alignment;
using ned::text::Buffer;
using ned::text::Rope;

TEST_CASE("FindTableAtPoint returns nullopt for a block with no delimiter row", "[Markdown]") {
    Buffer buffer("test", Rope("| just one line |\n"));
    buffer.SetPoint(2);
    REQUIRE_FALSE(FindTableAtPoint(buffer).has_value());
}

TEST_CASE("FindTableAtPoint returns nullopt when the second row isn't a valid delimiter row", "[Markdown]") {
    Buffer buffer("test", Rope("| Name | Age |\n| not a delimiter row |\n"));
    buffer.SetPoint(2);
    REQUIRE_FALSE(FindTableAtPoint(buffer).has_value());
}

TEST_CASE("FindTableAtPoint parses a valid table with default alignment", "[Markdown]") {
    Buffer buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    buffer.SetPoint(2);

    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table.has_value());
    REQUIRE(table->rows.size() == 2); // header + one data row
    REQUIRE(table->rows[0] == std::vector<std::string>{"Name", "Age"});
    REQUIRE(table->rows[1] == std::vector<std::string>{"Alice", "30"});
    REQUIRE(table->columnAlignments == std::vector<Alignment>{Alignment::Default, Alignment::Default});
}

TEST_CASE("FindTableAtPoint recognizes left/right/center alignment markers", "[Markdown]") {
    Buffer buffer("test", Rope("| A | B | C |\n|:---|---:|:---:|\n| a | b | c |\n"));
    buffer.SetPoint(2);

    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table.has_value());
    REQUIRE(table->columnAlignments == std::vector<Alignment>{Alignment::Left, Alignment::Right, Alignment::Center});
}

TEST_CASE("AlignTableAtPoint reports failure off a table", "[Markdown]") {
    Buffer buffer("test", Rope("plain text\n"));
    buffer.SetPoint(0);
    REQUIRE_FALSE(AlignTableAtPoint(buffer));
}

TEST_CASE("AlignTableAtPoint pads every column to its content's own width", "[Markdown]") {
    Buffer buffer("test", Rope("| N | Age |\n|---|---|\n| Alice | 3 |\n"));
    buffer.SetPoint(2);

    REQUIRE(AlignTableAtPoint(buffer));
    REQUIRE(buffer.Text() == "| N     | Age |\n|-------|-----|\n| Alice | 3   |\n");
}

TEST_CASE("AlignTableAtPoint right-aligns a column marked with a trailing colon", "[Markdown]") {
    Buffer buffer("test", Rope("| Item | Price |\n|---|---:|\n| Milk | 3 |\n| Eggs | 12 |\n"));
    buffer.SetPoint(2);

    REQUIRE(AlignTableAtPoint(buffer));
    REQUIRE(buffer.Text() == "| Item | Price |\n|------|------:|\n| Milk |     3 |\n| Eggs |    12 |\n");
}

TEST_CASE("AlignTableAtPoint advances point to the next cell", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    const std::size_t nameCell = buffer.Text().find("Alice");
    buffer.SetPoint(nameCell);

    REQUIRE(AlignTableAtPoint(buffer));

    const std::string result  = buffer.Text();
    const std::size_t ageCell = result.find("30");
    REQUIRE(buffer.Point() == ageCell);
}

TEST_CASE("AlignTableAtPoint wraps from the table's last cell back to the header's first", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    const std::size_t lastCell = buffer.Text().rfind("30");
    buffer.SetPoint(lastCell);

    REQUIRE(AlignTableAtPoint(buffer));

    const std::string result   = buffer.Text();
    const std::size_t nameCell = result.find("Name");
    REQUIRE(buffer.Point() == nameCell);
}
