//
// UI/Table/Layout.h and UI/Table/Order.h -- TableView's column geometry and
// line order, without a widget.
//

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "UI/Table/Layout.h"
#include "UI/Table/Order.h"

namespace {

using ned::ui::table::Cell;
using ned::ui::table::Column;
using ned::ui::table::ColumnSlot;
using ned::ui::table::Group;
using ned::ui::table::Line;
using ned::ui::table::Model;
using ned::ui::table::Row;
using ned::ui::table::Sort;

// Key | Title | Assignee | Age, the tracker's shape.
std::vector<Column> TrackerColumns() {
    return {Column{.id = "key"},
            Column{.id = "title", .width = Column::Width::Flex, .minWidth = 8},
            Column{.id = "assignee", .maxWidth = 12, .dropPriority = 2},
            Column{.id = "age", .dropPriority = 1}};
}

std::vector<std::size_t> Shown(const std::vector<ColumnSlot>& slots) {
    std::vector<std::size_t> columns;
    for (const ColumnSlot& slot : slots) {
        columns.push_back(slot.column);
    }
    return columns;
}

Row MakeRow(std::string id, std::vector<Cell> cells) {
    return Row{.id = std::move(id), .cells = std::move(cells)};
}

std::vector<std::string> RowIds(const Model& model, const std::vector<Line>& lines) {
    std::vector<std::string> ids;
    for (const Line& line : lines) {
        ids.push_back(line.kind == Line::Kind::GroupHeader ? "[" + model.groups[line.group].id + "]"
                                                           : model.groups[line.group].rows[line.row].id);
    }
    return ids;
}

} // namespace

TEST_CASE("Fit columns take their content width and a Flex column the rest", "[TableLayout]") {
    const std::vector<int> content{6, 30, 5, 3};
    const auto             slots = ned::ui::table::LayoutColumns(TrackerColumns(), content, 40);
    REQUIRE(slots.size() == 4);
    CHECK(slots[0] == ColumnSlot{.column = 0, .x = 0, .width = 6});
    // 40 - (6 + 5 + 3) - 3 gaps = 23
    CHECK(slots[1] == ColumnSlot{.column = 1, .x = 7, .width = 23});
    CHECK(slots[2] == ColumnSlot{.column = 2, .x = 31, .width = 5});
    CHECK(slots[3] == ColumnSlot{.column = 3, .x = 37, .width = 3});
}

TEST_CASE("A Fit column is clamped to its maximum", "[TableLayout]") {
    const std::vector<int> content{6, 30, 25, 3};
    const auto             slots = ned::ui::table::LayoutColumns(TrackerColumns(), content, 60);
    REQUIRE(slots.size() == 4);
    CHECK(slots[2].width == 12);
    CHECK(slots.back().x + slots.back().width == 60);
}

TEST_CASE("Columns drop in priority order as the width shrinks", "[TableLayout]") {
    const std::vector<int> content{6, 30, 8, 3};
    // Everything at minimum: 6 + 8 + 8 + 3 + 3 gaps = 28.
    CHECK(Shown(ned::ui::table::LayoutColumns(TrackerColumns(), content, 28)) == std::vector<std::size_t>{0, 1, 2, 3});
    CHECK(Shown(ned::ui::table::LayoutColumns(TrackerColumns(), content, 27)) == std::vector<std::size_t>{0, 1, 3});
    CHECK(Shown(ned::ui::table::LayoutColumns(TrackerColumns(), content, 19)) == std::vector<std::size_t>{0, 1, 3});
    CHECK(Shown(ned::ui::table::LayoutColumns(TrackerColumns(), content, 18)) == std::vector<std::size_t>{0, 1});
}

TEST_CASE("Past the last droppable column, Flex shrinks and the edge clips", "[TableLayout]") {
    const std::vector<int> content{6, 30, 8, 3};
    auto                   slots = ned::ui::table::LayoutColumns(TrackerColumns(), content, 10);
    REQUIRE(slots.size() == 2);
    CHECK(slots[1] == ColumnSlot{.column = 1, .x = 7, .width = 3});

    slots = ned::ui::table::LayoutColumns(TrackerColumns(), content, 4);
    REQUIRE(slots.size() == 1);
    CHECK(slots[0] == ColumnSlot{.column = 0, .x = 0, .width = 4});
    CHECK(ned::ui::table::LayoutColumns(TrackerColumns(), content, 0).empty());
}

TEST_CASE("Two Flex columns share the remainder, leftmost taking the odd cell", "[TableLayout]") {
    const std::vector<Column> columns{Column{.id = "a", .width = Column::Width::Flex, .minWidth = 2},
                                      Column{.id = "b", .width = Column::Width::Flex, .minWidth = 2}};
    const std::vector<int>    content{0, 0};
    const auto                slots = ned::ui::table::LayoutColumns(columns, content, 10);
    REQUIRE(slots.size() == 2);
    CHECK(slots[0].width == 5);
    CHECK(slots[1] == ColumnSlot{.column = 1, .x = 6, .width = 4});
}

TEST_CASE("Text compares case-insensitively with digit runs as numbers", "[TableLayout]") {
    const auto compare = [](std::string a, std::string b) {
        return ned::ui::table::CompareCells(Cell{.text = std::move(a)}, Cell{.text = std::move(b)});
    };
    CHECK(compare("NED-9", "NED-10") < 0);
    CHECK(compare("#12", "#9") > 0);
    CHECK(compare("file007", "file7") == 0);
    CHECK(compare("alpha", "Beta") < 0);
    CHECK(compare("abc", "abcd") < 0);
    CHECK(ned::ui::table::CompareCells(Cell{.text = "1.0K", .sortNumber = 1024}, Cell{.text = "900B", .sortNumber = 900}) > 0);
}

TEST_CASE("Rows sort within their groups, stably, with blanks last both ways", "[TableLayout]") {
    Model model;
    model.columns = {Column{.id = "key"}, Column{.id = "age", .descendingFirst = true}, Column{.id = "flag", .sortable = false}};
    model.groups  = {Group{.id   = "open",
                           .rows = {MakeRow("NED-10", {{.text = "NED-10"}, {.text = "2d", .sortNumber = 200}}),
                                    MakeRow("NED-9", {{.text = "NED-9"}, {}}),
                                    MakeRow("NED-2", {{.text = "NED-2"}, {.text = "1h", .sortNumber = 300}})}},
                     Group{.id = "done", .rows = {MakeRow("NED-1", {{.text = "NED-1"}, {.text = "5w", .sortNumber = 100}})}}};

    CHECK(RowIds(model, ned::ui::table::BuildLines(model, std::nullopt, {})) ==
          std::vector<std::string>{"[open]", "NED-10", "NED-9", "NED-2", "[done]", "NED-1"});
    CHECK(RowIds(model, ned::ui::table::BuildLines(model, Sort{.columnId = "key"}, {})) ==
          std::vector<std::string>{"[open]", "NED-2", "NED-9", "NED-10", "[done]", "NED-1"});
    CHECK(RowIds(model, ned::ui::table::BuildLines(model, Sort{.columnId = "age"}, {})) ==
          std::vector<std::string>{"[open]", "NED-10", "NED-2", "NED-9", "[done]", "NED-1"});
    CHECK(RowIds(model, ned::ui::table::BuildLines(model, Sort{.columnId = "age", .descending = true}, {})) ==
          std::vector<std::string>{"[open]", "NED-2", "NED-10", "NED-9", "[done]", "NED-1"});
    // Unsortable and unknown columns leave the model's order.
    CHECK(RowIds(model, ned::ui::table::BuildLines(model, Sort{.columnId = "flag"}, {})) ==
          RowIds(model, ned::ui::table::BuildLines(model, std::nullopt, {})));
    CHECK(RowIds(model, ned::ui::table::BuildLines(model, Sort{.columnId = "nope"}, {})) ==
          RowIds(model, ned::ui::table::BuildLines(model, std::nullopt, {})));
}

TEST_CASE("A collapsed group keeps its header; a headerless group has none", "[TableLayout]") {
    Model model;
    model.columns = {Column{.id = "name"}};
    model.groups  = {Group{.id = "open", .rows = {MakeRow("a", {{.text = "a"}})}},
                     Group{.id = "done", .rows = {MakeRow("b", {{.text = "b"}})}}};
    CHECK(RowIds(model, ned::ui::table::BuildLines(model, std::nullopt, {"open"})) ==
          std::vector<std::string>{"[open]", "[done]", "b"});

    Model flat;
    flat.columns = {Column{.id = "name"}};
    flat.groups  = {Group{.rows = {MakeRow("a", {{.text = "a"}}), MakeRow("b", {{.text = "b"}})}}};
    CHECK(RowIds(flat, ned::ui::table::BuildLines(flat, std::nullopt, {""})) == std::vector<std::string>{"a", "b"});
}
