#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Markdown.h"
#include "Editor/Table.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::markdown::AlignTableAtPoint;
using ned::editor::markdown::DeleteTableColumnAtPoint;
using ned::editor::markdown::FindTableAtPoint;
using ned::editor::markdown::InsertTableColumnAtPoint;
using ned::editor::markdown::InsertTableRowAtPoint;
using ned::editor::markdown::KillTableRowAtPoint;
using ned::editor::markdown::MoveTableColumnLeftAtPoint;
using ned::editor::markdown::MoveTableColumnRightAtPoint;
using ned::editor::markdown::MoveTableRowDownAtPoint;
using ned::editor::markdown::MoveTableRowUpAtPoint;
using ned::editor::markdown::MoveToPreviousTableCellAtPoint;
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

TEST_CASE("AlignTableAtPoint appends a new data row when tabbing past the last cell", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    const std::size_t lastCell = buffer.Text().rfind("30");
    buffer.SetPoint(lastCell);

    REQUIRE(AlignTableAtPoint(buffer));

    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table.has_value());
    REQUIRE(table->rows.size() == 3); // header + Alice's row + the newly appended empty row
    REQUIRE(table->rows[2] == std::vector<std::string>{"", ""});

    // Point lands in the new row's first cell.
    const std::size_t newRowLineStart = buffer.Content().LineToByteOffset(table->endLine - 1);
    REQUIRE(buffer.Point() > newRowLineStart);
    REQUIRE(buffer.Content().ByteOffsetToLine(buffer.Point()) == table->endLine - 1);
}

TEST_CASE("MoveToPreviousTableCellAtPoint wraps from the header's first cell back to the last", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    const std::size_t nameCell = buffer.Text().find("Name");
    buffer.SetPoint(nameCell);

    REQUIRE(MoveToPreviousTableCellAtPoint(buffer));

    const std::string result  = buffer.Text();
    const std::size_t ageCell = result.rfind("30");
    REQUIRE(buffer.Point() == ageCell);
}

TEST_CASE("MoveToPreviousTableCellAtPoint reports failure off a table", "[Markdown]") {
    Buffer buffer("test", Rope("plain text\n"));
    buffer.SetPoint(0);
    REQUIRE_FALSE(MoveToPreviousTableCellAtPoint(buffer));
}

TEST_CASE("InsertTableRowAtPoint inserts an empty row above the current data row", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Bob | 40 |\n"));
    const std::size_t bobCell = buffer.Text().find("Bob");
    buffer.SetPoint(bobCell);

    REQUIRE(InsertTableRowAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table.has_value());
    REQUIRE(table->rows.size() == 3); // header + new empty row + Bob's row
    REQUIRE(table->rows[1] == std::vector<std::string>{"", ""});
    REQUIRE(table->rows[2] == std::vector<std::string>{"Bob", "40"});
}

TEST_CASE("InsertTableRowAtPoint from the header inserts the first data row", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Bob | 40 |\n"));
    const std::size_t nameCell = buffer.Text().find("Name");
    buffer.SetPoint(nameCell);

    REQUIRE(InsertTableRowAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table.has_value());
    REQUIRE(table->rows.size() == 3); // header + new empty row + Bob's row
    REQUIRE(table->rows[1] == std::vector<std::string>{"", ""});
    REQUIRE(table->rows[2] == std::vector<std::string>{"Bob", "40"});
}

TEST_CASE("KillTableRowAtPoint removes the current data row", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n| Bob | 40 |\n"));
    const std::size_t aliceCell = buffer.Text().find("Alice");
    buffer.SetPoint(aliceCell);

    REQUIRE(KillTableRowAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table.has_value());
    REQUIRE(table->rows.size() == 2); // header + Bob's row only
    REQUIRE(table->rows[1] == std::vector<std::string>{"Bob", "40"});
}

TEST_CASE("KillTableRowAtPoint refuses on the header row", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    const std::size_t nameCell = buffer.Text().find("Name");
    buffer.SetPoint(nameCell);
    REQUIRE_FALSE(KillTableRowAtPoint(buffer));
}

TEST_CASE("KillTableRowAtPoint leaves a header-and-delimiter-only table when the only data row is killed",
         "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    const std::size_t aliceCell = buffer.Text().find("Alice");
    buffer.SetPoint(aliceCell);

    REQUIRE(KillTableRowAtPoint(buffer));
    REQUIRE(buffer.Text() == "| Name | Age |\n|------|-----|\n");
}

TEST_CASE("MoveTableRowUpAtPoint swaps with the row above", "[Markdown]") {
    Buffer            buffer("test", Rope("| N |\n|---|\n| a |\n| b |\n"));
    const std::size_t bCell = buffer.Text().find("b");
    buffer.SetPoint(bCell);

    REQUIRE(MoveTableRowUpAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table->rows[1] == std::vector<std::string>{"b"});
    REQUIRE(table->rows[2] == std::vector<std::string>{"a"});
}

TEST_CASE("MoveTableRowUpAtPoint refuses on the first data row", "[Markdown]") {
    Buffer            buffer("test", Rope("| N |\n|---|\n| a |\n| b |\n"));
    const std::size_t aCell = buffer.Text().find("a");
    buffer.SetPoint(aCell);
    REQUIRE_FALSE(MoveTableRowUpAtPoint(buffer));
}

TEST_CASE("MoveTableRowDownAtPoint swaps with the row below", "[Markdown]") {
    Buffer            buffer("test", Rope("| N |\n|---|\n| a |\n| b |\n"));
    const std::size_t aCell = buffer.Text().find("a");
    buffer.SetPoint(aCell);

    REQUIRE(MoveTableRowDownAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table->rows[1] == std::vector<std::string>{"b"});
    REQUIRE(table->rows[2] == std::vector<std::string>{"a"});
}

TEST_CASE("MoveTableRowDownAtPoint refuses on the last data row", "[Markdown]") {
    Buffer            buffer("test", Rope("| N |\n|---|\n| a |\n| b |\n"));
    const std::size_t bCell = buffer.Text().find("b");
    buffer.SetPoint(bCell);
    REQUIRE_FALSE(MoveTableRowDownAtPoint(buffer));
}

TEST_CASE("InsertTableColumnAtPoint inserts an empty Default-aligned column to the right", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---:|\n| Alice | 30 |\n"));
    const std::size_t nameCell = buffer.Text().find("Name");
    buffer.SetPoint(nameCell);

    REQUIRE(InsertTableColumnAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table->rows[0] == std::vector<std::string>{"Name", "", "Age"});
    REQUIRE(table->columnAlignments == std::vector<Alignment>{Alignment::Default, Alignment::Default, Alignment::Right});
}

TEST_CASE("DeleteTableColumnAtPoint removes the current column and its alignment", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---:|\n| Alice | 30 |\n"));
    const std::size_t ageCell = buffer.Text().find("Age");
    buffer.SetPoint(ageCell);

    REQUIRE(DeleteTableColumnAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table->rows[0] == std::vector<std::string>{"Name"});
    REQUIRE(table->columnAlignments == std::vector<Alignment>{Alignment::Default});
}

TEST_CASE("DeleteTableColumnAtPoint refuses when only one column remains", "[Markdown]") {
    Buffer buffer("test", Rope("| Name |\n|---|\n| Alice |\n"));
    buffer.SetPoint(buffer.Text().find("Name"));
    REQUIRE_FALSE(DeleteTableColumnAtPoint(buffer));
}

TEST_CASE("MoveTableColumnLeftAtPoint swaps the column and its alignment with the one to the left", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---:|\n| Alice | 30 |\n"));
    const std::size_t ageCell = buffer.Text().find("Age");
    buffer.SetPoint(ageCell);

    REQUIRE(MoveTableColumnLeftAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table->rows[0] == std::vector<std::string>{"Age", "Name"});
    REQUIRE(table->columnAlignments == std::vector<Alignment>{Alignment::Right, Alignment::Default});
}

TEST_CASE("MoveTableColumnLeftAtPoint refuses on the first column", "[Markdown]") {
    Buffer buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    buffer.SetPoint(buffer.Text().find("Name"));
    REQUIRE_FALSE(MoveTableColumnLeftAtPoint(buffer));
}

TEST_CASE("MoveTableColumnRightAtPoint swaps the column and its alignment with the one to the right", "[Markdown]") {
    Buffer            buffer("test", Rope("| Name | Age |\n|---|---:|\n| Alice | 30 |\n"));
    const std::size_t nameCell = buffer.Text().find("Name");
    buffer.SetPoint(nameCell);

    REQUIRE(MoveTableColumnRightAtPoint(buffer));
    const auto table = FindTableAtPoint(buffer);
    REQUIRE(table->rows[0] == std::vector<std::string>{"Age", "Name"});
    REQUIRE(table->columnAlignments == std::vector<Alignment>{Alignment::Right, Alignment::Default});
}

TEST_CASE("MoveTableColumnRightAtPoint refuses on the last column", "[Markdown]") {
    Buffer buffer("test", Rope("| Name | Age |\n|---|---|\n| Alice | 30 |\n"));
    buffer.SetPoint(buffer.Text().find("Age"));
    REQUIRE_FALSE(MoveTableColumnRightAtPoint(buffer));
}
