//
// TableView (Source/UI/TableView.h) -- painted into a Screen and driven by
// synthetic events, headless.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "TestEvents.h"
#include "UI/TableView.h"
#include "UI/Widget.h"

namespace {

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::MouseEvent;
using ned::ui::Screen;
using ned::ui::TableView;
using ned::ui::Theme;
using ned::ui::table::Cell;
using ned::ui::table::Column;
using ned::ui::table::Group;
using ned::ui::table::Model;
using ned::ui::table::Row;
using ned::ui::table::Sort;
namespace test = ned::ui::test;

struct Fixture {
    Theme     theme = ned::ui::DarkTheme();
    TableView table{theme};
    Screen    screen;
    int       width;
    int       height;

    std::vector<std::string> activated;
    std::vector<std::string> keys;
    int                      cancels = 0;

    Fixture(int w, int h) : screen(w, h), width(w), height(h) {
        table.SetDrawBorder(false);
        table.SetBox_(Box{.x_min = 0, .x_max = w - 1, .y_min = 0, .y_max = h - 1});
        table.TakeFocus();
        table.SetOnActivate([this](const std::string& id) { activated.push_back(id); });
        table.SetOnKey([this](const ned::editor::KeyChord& chord) { keys.push_back(std::string(1, static_cast<char>(chord.Codepoint))); });
        table.SetOnCancel([this] { ++cancels; });
    }

    void Paint() {
        table.Paint(Canvas(screen, table.Box_()));
    }

    [[nodiscard]] std::string RowText(int y) {
        std::string text;
        for (int x = 0; x < width; ++x) {
            text += screen.PixelAt(x, y).character;
        }
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
        return text;
    }

    [[nodiscard]] std::string Char(int x, int y) {
        return screen.PixelAt(x, y).character;
    }

    void Press(const ned::ui::Event& event) {
        REQUIRE(table.OnEvent(event));
    }

    void Click(int x, int y) {
        Press(test::Mouse(x, y, MouseEvent::Button::Left, MouseEvent::Motion::Pressed));
    }
};

Row MakeRow(std::string id, std::vector<Cell> cells) {
    return Row{.id = std::move(id), .cells = std::move(cells)};
}

// Name | Size, a flat list.
Model Files() {
    Model model;
    model.columns = {Column{.id = "name", .header = "Name", .width = Column::Width::Flex, .minWidth = 4},
                     Column{.id              = "size",
                            .header          = "Size",
                            .align           = ned::ui::table::Align::Right,
                            .dropPriority    = 1,
                            .descendingFirst = true}};
    model.groups  = {Group{.rows = {MakeRow("beta", {{.text = "beta"}, {.text = "2.0K", .sortNumber = 2048}}),
                                    MakeRow("alpha", {{.text = "alpha"}, {.text = "10B", .sortNumber = 10}}),
                                    MakeRow("gamma", {{.text = "gamma"}, {.text = "500B", .sortNumber = 500}})}}};
    return model;
}

// Key | Title under two groups.
Model Issues() {
    Model model;
    model.columns = {Column{.id = "key", .header = "Key"},
                     Column{.id = "title", .header = "Title", .width = Column::Width::Flex, .minWidth = 4}};
    model.groups  = {Group{.id    = "open",
                           .label = "Open",
                           .right = "2",
                           .rows  = {MakeRow("A-1", {{.text = "A-1"}, {.text = "Crash"}}),
                                     MakeRow("A-2", {{.text = "A-2"}, {.text = "Slow"}})}},
                     Group{.id = "done", .label = "Done", .right = "1", .rows = {MakeRow("A-3", {{.text = "A-3"}, {.text = "Typo"}})}}};
    return model;
}

} // namespace

TEST_CASE("Headers and cells share their column's alignment", "[TableView]") {
    Fixture f(24, 6);
    f.table.SetModel(Files());
    f.Paint();
    // Padding at x=0, Name from x=1; Size right-aligned against x=22.
    CHECK(f.RowText(0) == " Name              Size");
    CHECK(f.RowText(1) == " beta              2.0K");
    CHECK(f.RowText(2) == " alpha              10B");
    CHECK(f.Char(23, 1) == " ");
}

TEST_CASE("A narrow table drops its droppable column and cuts long text", "[TableView]") {
    Fixture f(10, 4);
    Model   model                         = Files();
    model.groups[0].rows[0].cells[0].text = "averyverylongname";
    f.table.SetModel(model);
    f.Paint();
    CHECK(f.RowText(0) == " Name");
    CHECK(f.RowText(1) == " averyve…");
    CHECK(f.RowText(2) == " alpha");
}

TEST_CASE("S steps through the sortable columns and back; R reverses", "[TableView]") {
    Fixture f(24, 6);
    f.table.SetModel(Files());

    f.Press(test::Character('R')); // nothing to reverse yet
    CHECK_FALSE(f.table.CurrentSort().has_value());

    f.Press(test::Character('S'));
    CHECK(f.table.CurrentSort() == Sort{.columnId = "name"});
    f.Paint();
    CHECK(f.RowText(0) == " Name ▴            Size");
    CHECK(f.RowText(1).starts_with(" alpha"));
    CHECK(f.RowText(3).starts_with(" gamma"));

    f.Press(test::Character('S'));
    CHECK(f.table.CurrentSort() == Sort{.columnId = "size", .descending = true});
    f.Paint();
    CHECK(f.RowText(0) == " Name            ▾ Size");
    CHECK(f.RowText(1).starts_with(" beta"));
    CHECK(f.RowText(2).starts_with(" gamma"));
    CHECK(f.RowText(3).starts_with(" alpha"));

    f.Press(test::Character('R'));
    f.Paint();
    CHECK(f.RowText(1).starts_with(" alpha"));

    f.Press(test::Character('S'));
    CHECK_FALSE(f.table.CurrentSort().has_value());
    f.Paint();
    CHECK(f.RowText(1).starts_with(" beta"));
    CHECK(f.keys.empty());
}

TEST_CASE("A header click sorts by that column, and again reverses it", "[TableView]") {
    Fixture f(24, 6);
    f.table.SetModel(Files());
    f.Click(20, 0);
    CHECK(f.table.CurrentSort() == Sort{.columnId = "size", .descending = true});
    f.Click(20, 0);
    CHECK(f.table.CurrentSort() == Sort{.columnId = "size", .descending = false});
    f.Click(2, 0);
    CHECK(f.table.CurrentSort() == Sort{.columnId = "name", .descending = false});
    CHECK(f.activated.empty());
}

TEST_CASE("The selection follows its row through a sort and a push", "[TableView]") {
    Fixture f(24, 6);
    f.table.SetModel(Files());
    f.Press(test::ArrowDown());
    f.Press(test::ArrowDown());
    CHECK(f.table.SelectedRowId() == "gamma");

    f.Press(test::Character('S')); // name: alpha, beta, gamma
    CHECK(f.table.SelectedRowId() == "gamma");
    f.Press(test::Character('R')); // gamma, beta, alpha
    CHECK(f.table.SelectedRowId() == "gamma");

    Model reordered = Files();
    reordered.groups[0].rows.insert(reordered.groups[0].rows.begin(), MakeRow("delta", {{.text = "delta"}}));
    f.table.SetModel(reordered);
    CHECK(f.table.SelectedRowId() == "gamma");

    // Gone: the selection keeps its position.
    Model without = Files();
    std::erase_if(without.groups[0].rows, [](const Row& row) { return row.id == "gamma"; });
    f.table.SetModel(without);
    CHECK(f.table.SelectedRowId() == "beta");

    f.table.SelectRow("alpha");
    CHECK(f.table.SelectedRowId() == "alpha");
    // Still sorted name-descending: beta, alpha.
    f.table.MoveSelection(-5);
    CHECK(f.table.SelectedRowId() == "beta");
    f.table.MoveSelection(5);
    CHECK(f.table.SelectedRowId() == "alpha");
}

TEST_CASE("Groups open, close and survive a push", "[TableView]") {
    Fixture f(24, 8);
    f.table.SetModel(Issues());
    f.Paint();
    CHECK(f.RowText(0) == "   Key Title");
    CHECK(f.RowText(1) == " ▾ Open               2");
    CHECK(f.RowText(2) == "   A-1 Crash");

    f.Press(test::ArrowDown()); // A-1
    f.Press(test::ArrowLeft()); // climbs to its header
    CHECK_FALSE(f.table.SelectedRowId().has_value());
    f.Paint();
    CHECK(f.RowText(2) == "   A-1 Crash");

    f.Press(test::ArrowLeft()); // closes it
    f.Paint();
    CHECK(f.RowText(1) == " ▸ Open               2");
    CHECK(f.RowText(2) == " ▾ Done               1");

    f.table.SetModel(Issues());
    f.Paint();
    CHECK(f.RowText(2) == " ▾ Done               1");

    f.Press(test::ArrowRight());
    f.Paint();
    CHECK(f.RowText(2) == "   A-1 Crash");

    // Enter toggles a header rather than activating anything; a click on one
    // does the same.
    f.Press(test::End());
    f.Press(test::ArrowUp()); // the Done header
    f.Press(test::Return());
    f.Paint();
    CHECK(f.RowText(5).empty());
    f.Click(5, 4);
    f.Paint();
    CHECK(f.RowText(5) == "   A-3 Typo");
    CHECK(f.activated.empty());

    // Loading marks every header.
    Model loading   = Issues();
    loading.loading = true;
    f.table.SetModel(loading);
    f.Paint();
    CHECK(f.Char(1, 1) == "…");
}

TEST_CASE("Rows activate on Enter, a click and a digit", "[TableView]") {
    Fixture f(24, 8);
    f.table.SetModel(Issues());
    f.Press(test::ArrowDown());
    f.Press(test::Return());
    f.Click(8, 3);
    f.Press(test::Character('3')); // ignored until digits are turned on
    f.table.SetDigitActivate(true);
    f.Press(test::Character('3')); // the third row, headers not counted
    CHECK(f.activated == std::vector<std::string>{"A-1", "A-2", "A-3"});
    CHECK(f.keys == std::vector<std::string>{"3"});
    CHECK(f.table.SelectedRowId() == "A-3");

    f.Press(test::Escape());
    CHECK(f.cancels == 1);
}

TEST_CASE("A cell click can be taken before it activates the row", "[TableView]") {
    Fixture                  f(24, 8);
    std::vector<std::string> clicks;
    f.table.SetOnCellClick([&](const std::string& id, std::size_t column, int offset) {
        clicks.push_back(id + ":" + std::to_string(column) + ":" + std::to_string(offset));
        return column == 0;
    });
    f.table.SetModel(Issues());
    f.Click(5, 3); // "A-2", its third cell
    CHECK(clicks == std::vector<std::string>{"A-2:0:2"});
    CHECK(f.activated.empty());
    CHECK(f.table.SelectedRowId() == "A-2");
    f.Click(8, 2); // Title
    CHECK(clicks.back() == "A-1:1:1");
    CHECK(f.activated == std::vector<std::string>{"A-1"});
}

TEST_CASE("An empty table shows its placeholder and still takes keys", "[TableView]") {
    Fixture f(24, 4);
    Model   model = Files();
    model.groups.clear();
    model.placeholder = "Press g to load";
    f.table.SetModel(model);
    f.Paint();
    CHECK(f.RowText(1) == " Press g to load");
    f.Press(test::Return());
    f.Press(test::Character('g'));
    f.Click(3, 1);
    CHECK(f.activated == std::vector<std::string>{"", ""});
    CHECK(f.keys == std::vector<std::string>{"g"});
}

TEST_CASE("Paging moves the selection and the window follows it", "[TableView]") {
    Fixture f(20, 6); // a header and five lines
    Model   model;
    model.columns = {Column{.id = "n", .header = "N"}};
    model.groups.emplace_back();
    for (int i = 0; i < 20; ++i) {
        model.groups[0].rows.push_back(MakeRow(std::to_string(i), {{.text = "row" + std::to_string(i)}}));
    }
    f.table.SetModel(model);
    f.Press(test::PageDown());
    CHECK(f.table.SelectedRowId() == "4");
    f.Press(test::End());
    f.Paint();
    CHECK(f.RowText(5) == " row19");
    CHECK(f.RowText(1) == " row15");
    f.Press(test::Home());
    f.Paint();
    CHECK(f.RowText(1) == " row0");
    f.Press(test::ArrowUp()); // wraps
    CHECK(f.table.SelectedRowId() == "19");
}

TEST_CASE("With its own border the table draws the title", "[TableView]") {
    Fixture f(24, 6);
    f.table.SetDrawBorder(true);
    Model model = Files();
    model.title = "Buffers";
    f.table.SetModel(model);
    f.Paint();
    CHECK(f.RowText(0).find("Buffers") != std::string::npos);
    CHECK(f.RowText(1).find("Name") != std::string::npos);
    CHECK(f.RowText(2).find("beta") != std::string::npos);
}
